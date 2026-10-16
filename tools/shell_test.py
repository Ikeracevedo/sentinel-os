#!/usr/bin/env python3
"""Pruebas automaticas del shell de Sentinel OS: conversa con la placa por el puerto serie.

Uso (en WSL, con la placa flasheada en modo shell):
    python3 tools/shell_test.py              # bateria completa (CP4)
    python3 tools/shell_test.py --soak 3600  # uptime: comandos periodicos durante N segundos

El canal USB pierde algun byte de vez en cuando: cada comando se reintenta si no aparece el
prompt, y las comprobaciones buscan fragmentos clave, no salidas exactas.
"""
import glob
import re
import sys
import time

import serial

BAUD = 57600
PROMPT_RE = re.compile(rb'sentinel:[^\r\n$]*\$ $')
ANSI_RE = re.compile(rb'\x1b\[[0-9;]*[A-Za-z]')

passed = 0
failed = []


def find_port():
    ports = sorted(glob.glob('/dev/ttyUSB*'), key=lambda p: int(p[len('/dev/ttyUSB'):]))
    return ports[-1]


class Shell:
    def __init__(self):
        self.s = serial.Serial(find_port(), BAUD, timeout=0.2)

    def reset(self):
        self.s.setDTR(False)
        self.s.setRTS(True)
        time.sleep(0.1)
        self.s.setRTS(False)

    def read_until_prompt(self, timeout=6.0):
        buf = b''
        t0 = time.time()
        while time.time() - t0 < timeout:
            d = self.s.read(max(1, self.s.in_waiting))
            if d:
                buf += d
                if PROMPT_RE.search(buf):
                    return buf, True
        return buf, False

    def boot(self):
        self.reset()
        buf, ok = self.read_until_prompt(timeout=12)
        return buf, ok

    def raw(self, data, timeout=6.0):
        """Envia bytes tal cual y espera el prompt. Devuelve (salida, vio_prompt)."""
        self.s.reset_input_buffer()
        self.s.write(data)
        return self.read_until_prompt(timeout)

    def run(self, line, timeout=6.0, retries=2):
        """Ejecuta un comando y devuelve su salida (sin eco ni prompt)."""
        for attempt in range(retries + 1):
            buf, ok = self.raw(line.encode() + b'\r', timeout)
            if ok:
                text = ANSI_RE.sub(b'', buf).decode(errors='replace')
                text = text.replace('\r', '')
                lines = text.split('\n')
                body = lines[1:-1] if len(lines) > 1 else []     # sin eco ni prompt
                return '\n'.join(body)
            self.s.write(b'\r')                                    # reenganchar el prompt
            self.read_until_prompt(3)
        return '<<sin prompt>>'

    def prompt_line(self):
        buf, ok = self.raw(b'\r', 4)
        m = PROMPT_RE.search(buf)
        return m.group(0).decode(errors='replace') if m else ''


def check(name, cond, detail=''):
    global passed
    if cond:
        passed += 1
        print('[PASS] ' + name)
    else:
        failed.append(name)
        print('[FAIL] ' + name + ('  -> ' + detail if detail else ''))


def usado(sh):
    out = sh.run('free')
    m = re.search(r'Usado:\s+(\d+)', out)
    return int(m.group(1)) if m else None


def main_battery(sh):
    banner, ok = sh.boot()
    txt = banner.decode(errors='replace')
    check('arranque: aparece el prompt del shell', ok)
    check('banner de bienvenida y motd', 'shell' in txt and 'Sentinel OS v0.1 - built' in txt, txt[-200:])
    check('prompt inicial es sentinel:/home/iker$', 'sentinel:/home/iker$ ' in txt)

    # ---- ls / cd / pwd (requisito explicito) ----
    check('pwd imprime /home/iker', sh.run('pwd').strip() == '/home/iker')
    out = sh.run('ls')
    check('ls muestra readme.txt y notas/ con tipo y permisos', 'readme.txt' in out and 'notas/' in out and 'drwx' in out and '-rw-' in out, out)
    sh.run('cd notas')
    check('cd notas: el prompt refleja el nuevo directorio', sh.prompt_line() == 'sentinel:/home/iker/notas$ ')
    check('pwd tras cd', sh.run('pwd').strip() == '/home/iker/notas')
    sh.run('cd ../..')
    check('cd ../.. sube dos niveles', sh.prompt_line() == 'sentinel:/home$ ')
    sh.run('cd')
    check('cd sin argumentos va a /home/iker', sh.prompt_line() == 'sentinel:/home/iker$ ')
    sh.run('cd /')
    sh.run('cd ..')
    check('cd .. en la raiz no falla y se queda en /', sh.prompt_line() == 'sentinel:/$ ')
    check('cd inexistente: mensaje legible', 'cd: no existe el directorio: /hoem' in sh.run('cd /hoem'))
    check('cd a un archivo: mensaje legible', 'cd: no es un directorio: /etc/motd' in sh.run('cd /etc/motd'))
    out = sh.run('ls /etc')
    check('ls /etc lista motd y version', 'motd' in out and 'version' in out, out)
    out = sh.run('ls /etc/motd')
    check('ls sobre un archivo muestra solo ese archivo', 'motd' in out and 'version' not in out, out)
    sh.run('cd /home/iker')

    # ---- cat / archivos ----
    check('cat /etc/motd', 'Sentinel OS' in sh.run('cat /etc/motd'))
    check('cat de un directorio: error claro', 'cat: es un directorio: notas' in sh.run('cat notas'))
    check('cat inexistente: error claro', 'cat: no existe: nada' in sh.run('cat nada'))

    base = usado(sh)
    check('free: se puede leer el heap usado', base is not None)
    sh.run('mkdir /tmp/prueba')
    check('mkdir duplicado: error claro', 'mkdir: ya existe: /tmp/prueba' in sh.run('mkdir /tmp/prueba'))
    check('mkdir con padre inexistente: error claro', 'mkdir: no existe el directorio padre de' in sh.run('mkdir /nada/x'))
    sh.run('write /tmp/prueba/a.txt hola mundo')
    check('write + cat: el texto vuelve intacto', 'hola mundo' in sh.run('cat /tmp/prueba/a.txt'))
    sh.run('touch /tmp/prueba/vacio')
    out = sh.run('ls /tmp/prueba')
    check('touch crea un archivo vacio (tamano 0) y ls ve ambos', 'a.txt' in out and 'vacio' in out, out)
    after_create = usado(sh)
    check('free: crear archivos consume heap visible', after_create is not None and base is not None and after_create > base)
    check('rm de un directorio no vacio falla con mensaje', 'no esta vacio' in sh.run('rm /tmp/prueba'))
    sh.run('rm /tmp/prueba/a.txt')
    sh.run('rm /tmp/prueba/vacio')
    sh.run('rm /tmp/prueba')
    check('rm libera la memoria: heap exactamente como al inicio', usado(sh) == base)
    sh.run('mkdir /tmp/r')
    sh.run('mkdir /tmp/r/a')
    sh.run('write /tmp/r/a/f x')
    rm_tree_out = sh.run('rm -r /tmp/r')
    tree_used = usado(sh)
    tree_listing = sh.run('ls /tmp/r')
    check('rm -r borra el arbol y deja el heap intacto',
          tree_used == base and 'no existe' in tree_listing,
          'base=%s despues=%s rm=%r ls=%r' %
          (base, tree_used, rm_tree_out, tree_listing))
    check('rm inexistente: error claro', 'rm: no existe: nada' in sh.run('rm nada'))

    # ---- ps / free / run / kill ----
    out = sh.run('ps')
    check('ps: el shell aparece y esta RUNNING', re.search(r'shell\s+RUNNING', out) is not None, out)
    check('ps: aparece idle', 'idle' in out)
    check('ps -v: columnas extendidas (pila)', 'PILA' in sh.run('ps -v'))
    out = sh.run('run counter')
    m = re.search(r'tarea (\d+) \(counter\) creada', out)
    check('run counter devuelve el tid', m is not None, out)
    tid = m.group(1) if m else '99'
    check('ps: el nuevo proceso aparece', 'counter' in sh.run('ps'))
    check('kill termina la tarea', 'terminada' in sh.run('kill ' + tid))
    check('ps: desaparece tras kill', 'counter' not in sh.run('ps'))
    check('tras run + kill el heap queda exacto', usado(sh) == base)
    m = re.search(r'tarea (\d+) \(alloc\) creada', sh.run('run alloc'))
    tid = m.group(1) if m else '99'
    time.sleep(0.5)
    out = sh.run('ps')
    mem = re.search(r'alloc\s+\S+\s+\d+\s+\d+%\s+(\d+)', out)
    check('ps: la tarea alloc muestra su memoria (>= 1024 B)', mem is not None and int(mem.group(1)) >= 1024, out)
    check('free refleja esa memoria', usado(sh) is not None and usado(sh) > base)
    sh.run('kill ' + tid)
    check('kill recupera la memoria de la tarea (heap exacto)', usado(sh) == base)
    check('kill de idle prohibido', 'no se puede matar a idle' in sh.run('kill 0'))
    shell_tid = re.search(r'\s*(\d+)\s+shell', sh.run('ps'))
    check('kill del propio shell prohibido', shell_tid is not None and 'propio shell' in sh.run('kill ' + shell_tid.group(1)))
    check('kill de un tid inexistente: error claro', 'no existe la tarea 14' in sh.run('kill 14'))
    check('run con programa desconocido: error claro', 'programa desconocido' in sh.run('run nada'))
    check('run sin argumentos lista los programas', 'blink' in sh.run('run') and 'hog' in sh.run('run'))

    # ---- /proc y sistema ----
    free_out = sh.run('free')
    meminfo = sh.run('cat /proc/meminfo')
    check('free coincide con cat /proc/meminfo', re.search(r'Usado:\s+(\d+)', free_out).group(1) == re.search(r'Usado:\s+(\d+)', meminfo).group(1))
    check('free muestra fragmentacion y pico', 'Fragmentacion' in free_out and 'Pico' in free_out)
    check('free -v dibuja el mapa del heap', '#' in sh.run('free -v') or '.' in sh.run('free -v'))
    check('/proc/tasks lista el shell', 'shell' in sh.run('cat /proc/tasks'))
    check('/proc/version', 'Sentinel OS' in sh.run('cat /proc/version'))
    check('uptime con formato Xd Xh Xm Xs', re.search(r'up \d+d \d+h \d+m \d+s', sh.run('uptime')) is not None)
    check('uname -a', 'Sentinel OS' in sh.run('uname -a'))
    out = sh.run('help')
    check('help lista los comandos', all(c in out for c in ('ls', 'cd', 'pwd', 'cat', 'ps', 'free', 'kill', 'run')), out)
    check('help <comando> da el uso', 'uso:' in sh.run('help ls'))
    check('echo con espacios multiples se colapsan', sh.run('echo hola    mundo').strip() == 'hola mundo')
    check('comando desconocido: mensaje', 'sentinel: comando no encontrado: xyz' in sh.run('xyz'))
    check('clear envia la secuencia ANSI', b'\x1b[2J' in sh.raw(b'clear\r')[0])

    # ---- edicion de linea ----
    check('backspace edita la linea', sh.run('ecco\x7f\x7fho hi').strip() == 'hi')
    buf, ok = sh.raw(b'abcdef\x03')
    check('Ctrl+C descarta la linea y vuelve el prompt', ok and b'comando no encontrado' not in buf)
    out = sh.run('a' * 200)
    m = re.search(r'comando no encontrado: (a+)', out)
    check('linea de 200 caracteres: se limita a 127 sin desbordar', m is not None and len(m.group(1)) == 127, out[:80])
    check('mas de 8 argumentos: error claro', 'demasiados argumentos' in sh.run('echo 1 2 3 4 5 6 7 8 9'))
    check('el sistema sigue respondiendo tras entrada invalida', sh.run('pwd').strip() == '/home/iker')

    # ---- historial ----
    sh.run('echo uno')
    sh.run('echo dos')
    out = sh.run('\x1b[A')                              # flecha arriba -> echo dos
    check('flecha arriba recupera el comando anterior y lo ejecuta', 'dos' in out, out)
    out = sh.run('\x1b[A\x1b[A')
    check('dos flechas arriba recuperan el anterior al anterior', 'dos' in out or 'uno' in out, out)
    hist = sh.run('history')
    check('history lista los comandos', 'echo' in hist and len(hist.splitlines()) >= 3, hist)

    # ---- estres: 200 comandos seguidos ----
    base2 = usado(sh)
    cmds = ['pwd', 'ls', 'ls /etc', 'echo x', 'cat /etc/version', 'cd /tmp', 'cd /home/iker', 'xyz',
            'mkdir /tmp/s', 'touch /tmp/s/f', 'write /tmp/s/f dato', 'cat /tmp/s/f', 'rm /tmp/s/f',
            'rm /tmp/s', 'cat /nada', 'ps', 'free', 'uptime', 'kill 99', 'run counter']
    bad = 0
    for i in range(200):
        c = cmds[i % len(cmds)]
        out = sh.run(c, timeout=8)
        if out == '<<sin prompt>>':
            bad += 1
        if c == 'run counter':
            m = re.search(r'tarea (\d+)', out)
            if m:
                sh.run('kill ' + m.group(1))
    check('200 comandos seguidos (validos e invalidos) sin colgarse', bad == 0, 'sin prompt: %d' % bad)
    check('tras el estres el heap esta exactamente como antes (sin fugas)', usado(sh) == base2)
    check('y el shell sigue vivo', sh.run('pwd').strip() == '/home/iker')

    # ---- reboot ----
    sh.s.reset_input_buffer()
    sh.s.write(b'reboot\r')
    buf, ok = sh.read_until_prompt(timeout=15)
    check('reboot reinicia el sistema y vuelve el shell', ok and b'Sentinel OS v0.1' in buf)


def soak(sh, seconds):
    sh.boot()
    base = usado(sh)
    cmds = ['pwd', 'ls', 'ps', 'free', 'uptime', 'cat /etc/motd', 'echo vivo']
    t0 = time.time()
    n = 0
    bad = 0
    last_report = t0
    while time.time() - t0 < seconds:
        out = sh.run(cmds[n % len(cmds)], timeout=8)
        n += 1
        if out == '<<sin prompt>>':
            bad += 1
        if time.time() - last_report > 300:
            print('[%ds] comandos=%d sin_respuesta=%d' % (time.time() - t0, n, bad), flush=True)
            last_report = time.time()
        time.sleep(2)
    final = usado(sh)
    up = sh.run('uptime')
    check('uptime continuo: el shell responde durante %d s' % seconds, bad == 0, 'sin respuesta: %d de %d' % (bad, n))
    check('uptime continuo: heap estable (sin fugas)', final == base, '%s -> %s' % (base, final))
    print('uptime reportado por el sistema:', up.strip())


if __name__ == '__main__':
    sh = Shell()
    if len(sys.argv) >= 3 and sys.argv[1] == '--soak':
        soak(sh, int(sys.argv[2]))
    else:
        main_battery(sh)
    print('\nSHELL TESTS: %d PASS, %d FAIL' % (passed, len(failed)))
    for f in failed:
        print('FALLO: ' + f)
    sys.exit(1 if failed else 0)

#!/usr/bin/env python3
"""Joga o jogo em ciclo fechado (agente): inicia uma instancia em modo TF_CTL e a comanda quadro a quadro.

  python tools/test/tf.py start [--fresh] [--exe gen/pkg/TagForceRecomp/TagForceRecomp_bg.exe] [--gpu] [--text o usuario]
  python tools/test/tf.py run "w300 X w60 START w100" [--shot nome]    # roteiro; imprime o quadro final
  python tools/test/tf.py shot [nome]                                  # grava gen/ctl/<nome>.png (padrao: shot) e imprime o caminho
  python tools/test/tf.py duelo [--oponente N] [--auto [--j0 N --j1 N --seed N --fim]]   # ate o inicio do Free Duel (ou ate o fim, com CPUvsCPU)
  python tools/test/tf.py peek 08800000 16 | poke ADDR HEX | ping | montage a b c | stop

Roteiro ("run"): palavras separadas por espaco.
  BOTAO      aperta 6 quadros e solta 8 (X=cruz, O=bola, SQ, TRI, START, SELECT, UP, DOWN, LEFT, RIGHT, L, R)
  BOTAO*N    aperta N quadros e solta 8        BOTAO+BOTAO   aperta juntos (ex. UP+X)
  wN         espera N quadros                  hold:BOTAO[+BOTAO] / release  mantem / solta
  shot:nome  captura gen/ctl/nome.png neste ponto
O jogo roda SEM limitador (rapido) e escondido; 1 quadro = 1/30 s do jogo. Usa copia do save em ./ms0 (nunca gen/ms0)."""
import os, sys, time, subprocess, shutil, glob, re, itertools

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CTL = os.path.join(ROOT, 'gen', 'ctl')
BTN = {'X': 0x4000, 'CROSS': 0x4000, 'O': 0x2000, 'CIRCLE': 0x2000, 'SQ': 0x8000, 'SQUARE': 0x8000, 'TRI': 0x1000, 'TRIANGLE': 0x1000,
       'START': 0x8, 'SELECT': 0x1, 'UP': 0x10, 'RIGHT': 0x20, 'DOWN': 0x40, 'LEFT': 0x80, 'L': 0x100, 'R': 0x200}
RAPIDO = False   # rasterizacao desligada entre capturas (duelo completo em ~1 min em vez de ~7)
_n = itertools.count(int(time.time()) % 100000 * 1000)


def send(cmd, timeout=600):
    rf = os.path.join(CTL, 'resp.txt')
    try: os.remove(rf)
    except OSError: pass
    i = 'c%d' % next(_n)
    tmp = os.path.join(CTL, 'cmd.tmp'); open(tmp, 'w').write('%s %s' % (i, cmd))
    os.replace(tmp, os.path.join(CTL, 'cmd.txt'))
    t0 = time.time(); last_check = t0
    while time.time() - t0 < timeout:
        try:
            s = open(rf).read()
            if s.startswith(i + ' '):
                r = s[len(i) + 1:].strip()
                if r.startswith('erro'): raise SystemExit('erro do jogo: ' + r)
                return r[3:].strip()   # "ok <quadro> [dados]" -> "<quadro> [dados]"
        except (FileNotFoundError, PermissionError): pass
        time.sleep(0.01)
        if time.time() - last_check > 2 and not (last_check := time.time()) is None and not vivo():
            raise SystemExit('o jogo FECHOU/TRAVOU (provavel CRASH). Veja o fim de gen/ctl/log.txt e gen/logs_teste/*.log:' + chr(10) + resumo_crash())
    raise SystemExit('sem resposta do jogo (travou ou fechou?). Veja gen/ctl/log.txt')


def vivo():
    try: pid = int(open(os.path.join(CTL, 'pid')).read())
    except Exception: return True
    r = subprocess.run(['tasklist', '/FI', 'PID eq %d' % pid, '/NH'], capture_output=True, text=True)
    return str(pid) in r.stdout


def resumo_crash():
    out = []
    for f in glob.glob(os.path.join(ROOT, 'gen', 'logs_teste', '*.log')):
        t = open(f, errors='ignore').read().splitlines()
        out += [l[:200] for l in t if 'CRASH' in l or 'RIP nativo' in l or 'ultimas funcoes' in l][-4:]
    return chr(10).join(out) or '(sem linha de crash no log)'


def log_texto():
    t = ''
    for f in glob.glob(os.path.join(ROOT, 'gen', 'logs_teste', '*.log')): t += open(f, errors='ignore').read()
    return t


def mask(tok):
    m = 0
    for p in tok.split('+'):
        if p.upper() not in BTN: raise SystemExit('botao desconhecido: ' + p)
        m |= BTN[p.upper()]
    return m


def shot(name='shot'):
    bmp = os.path.join(CTL, name + '.bmp')
    if RAPIDO: send('nodraw 0'); send('step 8')   # redesenha para a captura sair com imagem
    send('shot ' + bmp)
    if RAPIDO: send('nodraw 1')
    from PIL import Image
    Image.open(bmp).convert('RGB').save(os.path.join(CTL, name + '.png')); os.remove(bmp)
    return os.path.join(CTL, name + '.png')


def _amarelo(img):
    """Yellow menu header ("MAIN MENU", "FREE DUEL"...): count of yellow pixels in the top band of the picture."""
    im = img.convert('RGB'); w, h = im.size; band = im.crop((0, 0, w, h // 8)).convert('RGB').tobytes()
    return sum(1 for i in range(0, len(band), 3) if band[i] > 220 and band[i + 1] > 200 and band[i + 2] < 90)


def espera_tela(pred='viva', max_frames=3000, folga=60):
    """Waits (30-frame steps) until the picture satisfies `pred` ('viva' = not black, 'amarelo' = a yellow menu header is up), then `folga` more
    frames. Loads and fades vary in length, so fixed waits made the menu route flaky."""
    from PIL import Image
    q = 0
    while q < max_frames:
        send('step 30'); q += 30
        im = Image.open(shot('_viva'))
        if (pred == 'viva' and max(im.convert('L').getextrema()) > 40) or (pred == 'amarelo' and _amarelo(im) > im.size[0] * im.size[1] // 4000): break
    else: raise SystemExit('ERRO: espera "%s" estourou %d quadros. Veja gen/ctl/_viva.png' % (pred, max_frames))
    send('step %d' % folga)


def run(script):
    last = ''
    for t in script.split():
        if t.startswith('w') and t[1:].isdigit(): last = send('step ' + t[1:])
        elif t.startswith('shot:'): print('captura:', shot(t[5:]))
        elif t.startswith('hold:'): send('hold %x' % mask(t[5:]))
        elif t.startswith('pokectx:'):   # pokectx:DESLOCAMENTO:HEX = poke no bloco da tela de resultado (ponteiro em 09a151a4); so para testes
            _, off, val = t.split(':'); b = bytes.fromhex(send('peek 09a151a4 4').split()[-1]); ctx = int.from_bytes(b, 'little')
            send('poke %08x %s' % (ctx + int(off, 16), val))
        elif t.startswith('pokeptr:'):   # pokeptr:DESLOCAMENTO:DELTA:HEX = poke em (ponteiro guardado no bloco da tela de resultado + DELTA); so para testes
            _, off, delta, val = t.split(':'); b = bytes.fromhex(send('peek 09a151a4 4').split()[-1]); ctx = int.from_bytes(b, 'little')
            ptr = int.from_bytes(bytes.fromhex(send('peek %08x 4' % (ctx + int(off, 16))).split()[-1]), 'little')
            send('poke %08x %s' % (ptr + int(delta, 16), val))
        elif t.startswith('poke:'): print('poke', t[5:], send('poke ' + t[5:].replace(':', ' ')))   # poke:ENDERECO:HEX
        elif t.startswith('peek:'): print('peek', t[5:], send('peek ' + t[5:].replace(':', ' ')))   # peek:ENDERECO:BYTES (hex) dentro de um roteiro
        elif t in ('viva', 'amarelo'): espera_tela(t)   # waits for the picture to leave black / for a yellow menu header (+60 frames)
        elif t == 'texdump': send('texdump')   # o proximo quadro desenhado despeja gen/tex_*.bmp e os [prim] no log do jogo
        elif t == 'release': send('hold 0')
        elif t.startswith('analog:'): send('analog ' + t[7:].replace(':', ' '))   # analog:X:Y (0-255, 128 = centre) held until the next analog token
        else:
            n = 6
            if '*' in t: t, n = t.split('*'); n = int(n)
            send('hold %x' % mask(t)); send('step %d' % n); send('hold 0'); last = send('step 8')
    return last


def duelo(args):
    """Free Duel (1x1) ate o inicio do duelo. --oponente N (posicao na lista de duelistas, 0 = primeiro), --auto (plugin CPUvsCPU
    joga os dois lados), --j0 N/--j1 N (decks RCPnnn forcados; so com --auto), --lp N (padrao 2000), --seed N, --lento (com rasterizacao o tempo todo), --depois "roteiro" (apos o FIM), --fim (com --auto: avanca ate "[arena] FIM", max 40000 quadros)."""
    def opt(k, d=None): return args[args.index(k) + 1] if k in args else d
    envx = {}
    if '--auto' in args: envx['TF_CPUCPU'] = '1'
    if opt('--j0'): envx['TF_DECK_J0'] = opt('--j0')
    if opt('--j1'): envx['TF_DECK_J1'] = opt('--j1')
    if opt('--seed'): envx['TF_RNG_SEED'] = opt('--seed')
    os.environ.update(envx)
    start((['--rapido'] if '--lento' not in args else []) + (['--texturas'] if '--texturas' in args else []) + (['--pesado', '--texturas'] if '--pesado' in args else []))
    n = int(opt('--oponente', 0))
    run("w450 START w150 X w600 X amarelo DOWN w30 X w150")   # title (START only registers after ~350 frames) -> Continue (loads the save, no confirm box) -> main menu -> Free Duel
    lp = int(opt('--lp', 2000))   # LP 2000 por padrao: duelo curto para testes (regra alterada na tela do Free Duel)
    if lp == 8000: run('X w60 X w60 X w60 w60 X w500')                                  # Normal Duel, Single, standard rules, Next
    else: run('X w60 X w60 DOWN w20 X w60 ' + ' '.join(['DOWN w15'] * [2000, 4000, 8000, 12000, 16000].index(lp)) + ' X w60 X w60 X w500')   # ... Yes, LP, no time limit, Next
    if opt('--oponente') is not None: run('X w120 ' + ' '.join(['DOWN w15'] * n) + ' X w300')   # opponent list (empty when the save has no duelists unlocked yet): pick the N-th. Without --oponente the preselected opponent plays
    run('DOWN w25 DOWN w25 DOWN w25 w20')
    print('start duel:', shot('duelo_pronto'))
    run('X w200')
    for _ in range(14):   # pedra-papel-tesoura e ordem de jogada: aperta X ate o plugin ver LP > 0 (a partir dai o duelo e dele, com --auto)
        ls = [l for l in log_texto().splitlines() if '[arena] quadro' in l]
        if ls and 'LP 0 x 0' not in ls[-1]: break
        run('X w100')
    ls = [l for l in log_texto().splitlines() if '[arena] quadro' in l]
    if '--auto' in args and (not ls or 'LP 0 x 0' in ls[-1]):
        shot('duelo_erro'); stop(quiet=True); raise SystemExit('ERRO: o duelo nao comecou (rota do menu falhou). Veja gen/ctl/duelo_erro.png' + (' (com --oponente a lista de duelistas precisa ter itens: no save atual ela esta vazia; sem --oponente joga o oponente pre-selecionado)' if opt('--oponente') is not None else ''))
    print('inicio do duelo:', shot('duelo_inicio'))
    if '--auto' in args and '--fim' in args:
        q = 0
        while q < 40000 and '[arena] FIM' not in log_texto():
            run('w600'); q += 600
        t = log_texto(); i = t.find('[arena] FIM')
        print('final:', shot('duelo_fim'))
        if i < 0: stop(quiet=True); raise SystemExit('ERRO: sem FIM em %d quadros (duelo travado?). Veja gen/ctl/duelo_fim.png' % q)
        print(t[i:i + 200].splitlines()[0])
        if 'CRASH' in t: raise SystemExit('ERRO: CRASH no log')
        if '--depois' in args:   # roteiro tf.py run apos o FIM (ex.: --depois "w200 X w200 shot:res1"), antes de fechar
            print('quadro apos --depois:', run(args[args.index('--depois') + 1]))
        stop(quiet=True)


def save_source():
    """Most recent save of the user, read-only: the installed game keeps it in ./ms0 (next to the exe); gen/ms0 is the older location. TF_SAVE_FROM overrides."""
    c = [os.environ['TF_SAVE_FROM']] if os.environ.get('TF_SAVE_FROM') else [os.path.join(ROOT, 'ms0'), os.path.join(ROOT, 'gen', 'ms0')]
    c = [d for d in c if os.path.isdir(os.path.join(d, 'PSP', 'SAVEDATA'))]
    return max(c, key=lambda d: max([os.path.getmtime(f) for f in glob.glob(os.path.join(d, 'PSP', 'SAVEDATA', '*', '*'))] or [0])) if c else None


def start(args):
    if os.path.exists(os.path.join(CTL, 'pid')):
        try: stop(quiet=True)
        except SystemExit: pass
    shutil.rmtree(CTL, ignore_errors=True); os.makedirs(CTL)
    ini = open(os.path.join(ROOT, 'gen', 'tagforce_recomp.ini'), errors='ignore').read() if os.path.exists(os.path.join(ROOT, 'gen', 'tagforce_recomp.ini')) else ''
    iso = os.environ.get('TF_ISO') or (re.search(r'(?im)^iso=(.*)$', ini).group(1).strip() if re.search(r'(?im)^iso=(.*)$', ini) else '')
    exe = args[args.index('--exe') + 1] if '--exe' in args else os.path.join('gen', 'pkg', 'TagForceRecomp', 'TagForceRecomp_bg.exe')
    pkg = os.path.dirname(os.path.join(ROOT, exe))   # the test runs inside the package folder (its own gen/ translated code and ms0/ test save)
    dados = os.path.join(pkg, 'jogo')
    if not os.path.isdir(dados) and not os.path.isfile(iso): raise SystemExit('sem dados instalados em %s: rode tools/install_dev.sh ou defina TF_ISO' % dados)
    # save de teste = copia do save do usuario (ou vazio com --fresh); nunca escreve em gen/ms0
    src = save_source()   # pick the source BEFORE deleting pkg/ms0: with --exe pointing at the root exe, pkg/ms0 IS the player's live save
    if os.path.normcase(os.path.abspath(pkg)) == os.path.normcase(os.path.abspath(ROOT)):
        raise SystemExit('refusing to run the test inside the repo root: ./ms0 there is the live save of the user. Use the dev package (gen/pkg/TagForceRecomp) as --exe.')
    shutil.rmtree(os.path.join(pkg, 'ms0'), ignore_errors=True)
    if '--fresh' not in args and src: shutil.copytree(src, os.path.join(pkg, 'ms0'))
    mods = os.path.join(ROOT, 'gen', 'plugins_teste'); shutil.rmtree(mods, ignore_errors=True); os.makedirs(mods)
    for d in glob.glob(os.path.join(os.environ.get('TF_MODS_FROM') or os.path.join(ROOT, 'gen', 'mods'), '*')):   # TF_MODS_FROM=folder: test with only those mods
        if (os.path.isfile(os.path.join(d, 'plugin.dll')) or os.path.isdir(os.path.join(d, 'PSP_GAME'))) and not os.path.exists(os.path.join(d, 'disabled')):
            shutil.copytree(d, os.path.join(mods, os.path.basename(d)), ignore=shutil.ignore_patterns('llm'))
    if '--texturas' in args:   # liga o EnhancedTextures real (so leitura; mod.txt textures= aponta para a pasta dele) em GPU Vulkan 2x
        d = os.path.join(mods, 'EnhancedTextures'); os.makedirs(d)
        src = os.path.join(ROOT, 'mods', 'EnhancedTextures')
        with open(os.path.join(d, 'mod.txt'), 'w') as f: f.write('name=Enhanced Textures' + chr(10) + 'textures=' + os.path.join(src, 'textures') + chr(10))
        if os.path.exists(os.path.join(src, 'menu_estilo_pc.txt')): shutil.copyfile(os.path.join(src, 'menu_estilo_pc.txt'), os.path.join(d, 'menu_estilo_pc.txt'))
    logs = os.path.join(ROOT, 'gen', 'logs_teste'); shutil.rmtree(logs, ignore_errors=True)
    env = dict(os.environ, TF_CTL=CTL, TF_KEEP_CWD='1', TF_NO_MOVIES='1', NO_PACE='1', NO_AUDIO='1', TF_SEGUNDO_PLANO='1',
               TF_NO_ISO_CACHE='1', TF_INTERP='0', TF_LOG_DIR=logs, TF_INI=os.path.join(logs, 'teste.ini'), TF_MODS_DIR=mods, AUTO_TEXT=(args[args.index('--text') + 1] if '--text' in args else 'o usuario'))
    if '--texturas' in args: env.setdefault('TF_GPU', '2'); env.setdefault('TF_RES', '2')
    elif '--gpu' not in args: env.setdefault('TF_GPU', '0')
    if '--pesado' in args:   # config do usuario (gen/tagforce_recomp.ini): Vulkan 8x, MSAA, HD, limitador e interpolacao ligados (tempo real)
        env.update(TF_GPU='2', TF_RES='8'); env.pop('NO_PACE', None); env.pop('TF_INTERP', None)
        env.setdefault('TF_TEX_WAIT_MS', '4000')   # a rota do duelo espera os PNGs de textura; TF_TEX_WAIT_MS no ambiente (ex.: 80, o padrao do jogo) mede o jogo real
    if '--audio' in args:   # real-time audio test: keeps the pacing limiter and the audio device on (the window stays hidden)
        env.pop('NO_PACE', None); env.pop('NO_AUDIO', None); env.pop('TF_NO_MOVIES', None)
    log = open(os.path.join(CTL, 'log.txt'), 'wb')
    p = subprocess.Popen([os.path.join(ROOT, exe)] + (['--dados', dados] if os.path.isdir(dados) else ['--iso', iso]), cwd=pkg, env=env, stdout=log, stderr=log,
                         creationflags=0x00000008 | 0x00000200)   # DETACHED_PROCESS | NEW_PROCESS_GROUP
    open(os.path.join(CTL, 'pid'), 'w').write(str(p.pid))
    global RAPIDO
    RAPIDO = '--rapido' in args
    t0 = time.time()
    while time.time() - t0 < 180:
        if p.poll() is not None: raise SystemExit('o jogo fechou ao iniciar; veja gen/ctl/log.txt')
        if os.path.exists(os.path.join(CTL, 'cmd.txt')) is False:
            try:
                print('pronto, quadro', send('ping', timeout=2))
                if RAPIDO: send('nodraw 1')
                return
            except SystemExit: pass
        time.sleep(0.5)
    raise SystemExit('o jogo nao ficou pronto em 180 s')


def stop(quiet=False):
    try: send('quit', timeout=5)
    except SystemExit: pass
    pf = os.path.join(CTL, 'pid')
    if os.path.exists(pf):
        subprocess.run(['taskkill', '/F', '/PID', open(pf).read().strip()], capture_output=True); os.remove(pf)
    if not quiet: print('encerrado')


if __name__ == '__main__':
    a = sys.argv[1:]
    if not a: print(__doc__); sys.exit(0)
    c = a[0]
    if c == 'start': start(a[1:])
    elif c == 'duelo': duelo(a[1:])
    elif c == 'run':
        q = run(a[1]); print('quadro', q)
        if '--shot' in a: print('captura:', shot(a[a.index('--shot') + 1]))
    elif c == 'shot': print(shot(a[1] if len(a) > 1 else 'shot'))
    elif c in ('peek', 'poke'): print(send(' '.join(a)))
    elif c == 'montage':   # junta capturas em gen/ctl/montagem.png (2 colunas)
        from PIL import Image
        n = a[1:]; m = Image.new('RGB', (960, 272 * ((len(n) + 1) // 2)))
        for i, k in enumerate(n): m.paste(Image.open(os.path.join(CTL, k + ".png")).resize((480, 272)), ((i % 2) * 480, (i // 2) * 272))
        m.save(os.path.join(CTL, 'montagem.png')); print(os.path.join(CTL, 'montagem.png'))
    elif c == 'ping': print(send('ping'))
    elif c == 'stop': stop()
    else: print(__doc__)

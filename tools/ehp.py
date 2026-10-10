"""Pacotes EHP do Tag Force (textos, imagens, scripts): listar, extrair e reempacotar.

Formato (confirmado reempacotando todos os .ehp da ISO byte a byte, ver --teste):
  +0   "EHP\\x03"
  +4   u32 tamanho total do arquivo
  +8   4 bytes de etiqueta (normalmente "NOT ")
  +12  u32 n = numero de membros
  +16  n x (u32 deslocamento do nome, u32 deslocamento dos dados)
  depois: 8 bytes zero; os nomes ("nome\\0" + u32 tamanho do membro, sem alinhamento); os dados de cada membro
  alinhados em 16 bytes, na ordem da tabela; o arquivo termina alinhado em 16 (zeros depois do ultimo membro).
Membros ".gz" sao gzip (o jogo descomprime); extract grava o arquivo como esta (use --gunzip para descomprimir, e o
pack recomprime os que estavam como .gz).

Uso:
  python tools/ehp.py list    <arquivo.ehp | iso:PSP_GAME/USRDIR/...>
  python tools/ehp.py extract <arquivo.ehp | iso:...> <pasta> [--gunzip]
  python tools/ehp.py pack    <pasta> <saida.ehp>          (usa <pasta>/_ehp.txt, gravado pelo extract)
  python tools/ehp.py --teste                               (reempacota todos os .ehp da ISO e compara)
A ISO vem de --iso <arquivo> ou da variavel TF_ISO (required).
"""
import gzip
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from isofs import Iso  # noqa: E402


def ler(d):
    """-> (etiqueta, [(nome, bytes)])"""
    if d[:4] != b"EHP\x03":
        raise ValueError("nao e um EHP (assinatura %r)" % d[:4])
    tag, n = d[8:12], struct.unpack_from("<I", d, 12)[0]
    membros = []
    for i in range(n):
        no, do = struct.unpack_from("<II", d, 16 + 8 * i)
        e = d.index(b"\0", no)
        nome = d[no:e].decode("latin1")
        tam = struct.unpack_from("<I", d, e + 1)[0]
        membros.append((nome, bytes(d[do:do + tam])))
    return tag, membros


def montar(membros, tag=b"NOT "):
    n = len(membros)
    pos = 16 + 8 * n + 8
    nomes, offs_nome = bytearray(), []
    for nome, dados in membros:
        offs_nome.append(pos + len(nomes))
        nomes += nome.encode("latin1") + b"\0" + struct.pack("<I", len(dados))
    pos += len(nomes)
    corpo, offs_dados = bytearray(), []
    for _, dados in membros:
        pos_al = (pos + 15) & ~15
        corpo += b"\0" * (pos_al - pos)
        offs_dados.append(pos_al)
        corpo += dados
        pos = pos_al + len(dados)
    fim = (pos + 15) & ~15   # o arquivo termina alinhado em 16 (zeros)
    corpo += b"\0" * (fim - pos); pos = fim
    out = bytearray(b"EHP\x03" + struct.pack("<I", pos) + tag + struct.pack("<I", n))
    for a, b in zip(offs_nome, offs_dados):
        out += struct.pack("<II", a, b)
    out += b"\0" * 8 + nomes + corpo
    return bytes(out)


def _abrir(arg, iso):
    if arg.startswith("iso:"):
        return iso.ler(arg[4:])
    return open(arg, "rb").read()


def main(argv):
    iso_path = os.environ.get("TF_ISO", "")
    if "--iso" in argv:
        i = argv.index("--iso"); iso_path = argv[i + 1]; del argv[i:i + 2]
    iso = Iso(iso_path) if os.path.exists(iso_path) else None
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__); return 0
    cmd = argv[0]
    if cmd == "--teste":
        total = ok = 0
        for caminho, lba, tam in iso.arquivos():
            if not caminho.lower().endswith(".ehp"):
                continue
            d = iso.ler_lba(lba, tam); total += 1
            try:
                tag, m = ler(d)
                if montar(m, tag) == d: ok += 1
                else: print("DIFERENTE:", caminho)
            except Exception as e:
                print("ERRO:", caminho, e)
        print(f"{ok} de {total} EHP reempacotados identicos")
        return 0 if ok == total else 1
    if cmd == "list":
        tag, m = ler(_abrir(argv[1], iso))
        for nome, dados in m:
            print(f"{len(dados):9d}  {nome}")
        print(f"{len(m)} membros, etiqueta {tag!r}")
        return 0
    if cmd == "extract":
        tag, m = ler(_abrir(argv[1], iso)); pasta = argv[2]; gun = "--gunzip" in argv
        os.makedirs(pasta, exist_ok=True)
        with open(os.path.join(pasta, "_ehp.txt"), "w", encoding="utf-8") as man:
            man.write("tag=" + tag.decode("latin1") + "\n")
            for nome, dados in m:
                arq = nome
                if gun and nome.endswith(".gz"):
                    dados = gzip.decompress(dados); arq = nome[:-3]
                open(os.path.join(pasta, arq), "wb").write(dados)
                man.write(nome + ("\t" + arq if arq != nome else "") + "\n")
        print(f"{len(m)} membros em {pasta}")
        return 0
    if cmd == "pack":
        pasta, saida = argv[1], argv[2]
        linhas = open(os.path.join(pasta, "_ehp.txt"), encoding="utf-8").read().splitlines()
        tag = linhas[0][4:].encode("latin1") if linhas and linhas[0].startswith("tag=") else b"NOT "
        membros = []
        for l in linhas[1:]:
            if not l.strip():
                continue
            nome, _, arq = l.partition("\t")
            dados = open(os.path.join(pasta, arq or nome), "rb").read()
            if arq and nome.endswith(".gz"):
                dados = gzip.compress(dados, mtime=0)
            membros.append((nome, dados))
        os.makedirs(os.path.dirname(os.path.abspath(saida)), exist_ok=True)
        open(saida, "wb").write(montar(membros, tag))
        print(f"{saida}: {len(membros)} membros")
        return 0
    print(__doc__); return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

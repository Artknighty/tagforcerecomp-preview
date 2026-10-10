"""Banco de cartas do Tag Force 1 EU (duelsys/cardinfo_eng.ehp), decodificado.

Layout de CARD_Prop.bin (8 bytes por carta), tirado das funcoes de acesso no BOOT
(0x0884B4F8 e seguintes, exportadas como libehsys_*):
  w0 bits  0-12  id da carta (4000+)
  w0 bits 13-21  ATK/10   (0x1FF = "?")          libehsys_747753B8 / 760919BF
  w0 bits 22-30  DEF/10   (0x1FF = "?")          libehsys_F203CF01 / 249F5AD0
  w0 bit  31     flag (ver campo "b31")          libehsys_D53E9DC3
  w1 bits  0-3   f0                              libehsys_FCB00A02
  w1 bits  4-7   f4                              libehsys_E62CCE79
  w1 bits  8-11  f8                              libehsys_CF199BAE
  w1 bits 12-14  f12                             libehsys_A72D71BF
  w1 bits 15-19  f15                             libehsys_98E07D26
  w1 bits 20-22  f20                             libehsys_723A097D
O significado de f0..f20 esta em CAMPOS (confirmado com cartas conhecidas).

Uso:
    from cartas import CARTAS, por_nome
    c = por_nome("Blue-Eyes White Dragon"); print(c)
"""
import os, struct

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARQ = os.environ.get("TF_CARDINFO") or os.path.join(RAIZ, "gen", "jogo", "PSP_GAME", "USRDIR", "duelsys", "cardinfo_eng.ehp")   # dados do jogo instalados (tools/extrair_jogo.py)


def ehp(d, o=0):
    """Arquivo EHP: u32 n em +12; entradas (off_nome, off_dados); tamanho logo apos o nome."""
    n = struct.unpack_from("<I", d, o + 12)[0]
    out = {}
    for i in range(n):
        no, do = struct.unpack_from("<II", d, o + 16 + 8 * i)
        e = d.index(b"\0", o + no)
        nome = d[o + no:e].decode()
        tam = struct.unpack_from("<I", d, e + 1)[0]
        out[nome] = d[o + do:o + do + tam]
    return out


def _u16s(b, o):
    e = o
    while e + 1 < len(b) and b[e:e + 2] != b"\0\0":
        e += 2
    return b[o:e].decode("utf-16le", "replace")


# Significado dos campos (confirmado com cartas conhecidas + uso no codigo)
CATEGORIA = {0: "Normal", 1: "Efeito", 2: "Fusao", 3: "Fusao/Efeito", 4: "Ritual", 5: "Ritual/Efeito",
             6: "Toon", 7: "Espirito", 8: "Uniao", 9: "Ficha", 13: "Magia", 14: "Armadilha"}          # f0
ATRIBUTO = {0: "-", 1: "LUZ", 2: "TREVAS", 3: "AGUA", 4: "FOGO", 5: "TERRA", 6: "VENTO",
            8: "MAGIA", 9: "ARMADILHA"}                                                          # f4
ICONE = {0: "-", 1: "Resposta", 2: "Campo", 3: "Equipamento", 4: "Continua", 5: "Rapida", 6: "Ritual"}  # f12
TIPO = {0: "-", 1: "Dragao", 2: "Zumbi", 3: "Demonio", 4: "Piro", 5: "Serpente Marinha", 6: "Rocha",
        7: "Maquina", 8: "Peixe", 9: "Dinossauro", 10: "Inseto", 11: "Besta", 12: "Besta-Guerreira",
        13: "Planta", 14: "Aqua", 15: "Guerreiro", 16: "Besta Alada", 17: "Fada", 18: "Mago",
        19: "Trovao", 20: "Reptil", 22: "Magia", 23: "Armadilha"}                                 # f15
# f20: raridade. A loja (libshop) desenha com cor especial quando 3 ou 4 e com brilho quando != 0.
# Valores existentes: 0 (1513 cartas), 1 (510), 3 (300), 4 (156). Nomes provisorios:
RARIDADE = {0: "Comum", 1: "Rara", 2: "Super?", 3: "Super/Ultra?", 4: "Ultra?"}


class Carta:
    __slots__ = ("id", "idx", "nome", "desc", "atk", "defe", "b31", "f0", "f4", "f8", "f12", "f15", "f20", "senha", "w0", "w1")

    categoria = property(lambda s: CATEGORIA.get(s.f0, s.f0))
    atributo = property(lambda s: ATRIBUTO.get(s.f4, s.f4))
    nivel = property(lambda s: s.f8)
    icone = property(lambda s: ICONE.get(s.f12, s.f12))
    tipo = property(lambda s: TIPO.get(s.f15, s.f15))
    raridade = property(lambda s: RARIDADE.get(s.f20, s.f20))
    monstro = property(lambda s: s.f0 < 13)

    def __repr__(self):
        if self.monstro:
            return (f"<{self.id} {self.nome!r} {self.categoria} Nv{self.nivel} {self.atributo} {self.tipo} "
                    f"{self.atk}/{self.defe} [{self.raridade}]>")
        return f"<{self.id} {self.nome!r} {self.categoria} {self.icone} [{self.raridade}]>"


def _carregar():
    F = ehp(open(ARQ, "rb").read())
    prop, indx, nomes, desc = F["CARD_Prop.bin"], F["CARD_Indx_E.bin"], F["CARD_Name_E.bin"], F["CARD_Desc_E.bin"]
    senha = F["CARD_Pass.bin"]
    cartas = {}
    for i in range(len(prop) // 8):
        w0, w1 = struct.unpack_from("<II", prop, i * 8)
        c = Carta()
        c.w0, c.w1, c.idx = w0, w1, i
        c.id = w0 & 0x1FFF
        a, d = (w0 >> 13) & 0x1FF, (w0 >> 22) & 0x1FF
        c.atk = None if a == 0x1FF else a * 10
        c.defe = None if d == 0x1FF else d * 10
        c.b31 = w0 >> 31
        c.f0, c.f4, c.f8 = w1 & 0xF, (w1 >> 4) & 0xF, (w1 >> 8) & 0xF
        c.f12, c.f15, c.f20 = (w1 >> 12) & 7, (w1 >> 15) & 0x1F, (w1 >> 20) & 7
        no, do = struct.unpack_from("<II", indx, i * 8)
        c.nome, c.desc = _u16s(nomes, no), _u16s(desc, do)
        c.senha = struct.unpack_from("<I", senha, i * 4)[0] if i * 4 < len(senha) else None
        if c.id:
            cartas[c.id] = c
    return F, cartas


ARQUIVOS, CARTAS = _carregar()


def por_nome(nome):
    n = nome.lower()
    for c in CARTAS.values():
        if c.nome.lower() == n:
            return c
    return None

"""Leitura minima de ISO9660 (a ISO do usuario; nada do jogo vai para o repositorio)."""
import struct


class Iso:
    def __init__(self, caminho):
        self.f = open(caminho, "rb")
        pvd = self.ler_lba(16, 2048)
        if pvd[1:6] != b"CD001":
            raise ValueError("nao e uma ISO9660")
        raiz = pvd[156:156 + 34]
        self.raiz = (struct.unpack_from("<I", raiz, 2)[0], struct.unpack_from("<I", raiz, 10)[0])

    def ler_lba(self, lba, tam):
        self.f.seek(lba * 2048)
        return self.f.read(tam)

    def _entradas(self, lba, tam):
        d = self.ler_lba(lba, tam); i = 0
        while i < len(d):
            n = d[i]
            if n == 0:
                i = (i // 2048 + 1) * 2048; continue
            el, es, fl, nl = struct.unpack_from("<I", d, i + 2)[0], struct.unpack_from("<I", d, i + 10)[0], d[i + 25], d[i + 32]
            nome = d[i + 33:i + 33 + nl]
            if nome not in (b"\0", b"\1"):
                yield nome.decode("latin1").split(";")[0], el, es, bool(fl & 2)
            i += n

    def achar(self, caminho):
        """'PSP_GAME/USRDIR/x' (sem diferenciar maiusculas) -> (lba, tamanho)"""
        lba, tam = self.raiz
        for parte in caminho.strip("/").split("/"):
            for nome, el, es, _ in self._entradas(lba, tam):
                if nome.lower() == parte.lower():
                    lba, tam = el, es; break
            else:
                raise FileNotFoundError(caminho)
        return lba, tam

    def ler(self, caminho):
        return self.ler_lba(*self.achar(caminho))

    def arquivos(self, lba=None, tam=None, pref=""):
        """todos os arquivos: (caminho, lba, tamanho)"""
        if lba is None:
            lba, tam = self.raiz
        for nome, el, es, d in self._entradas(lba, tam):
            if d:
                yield from self.arquivos(el, es, pref + nome + "/")
            else:
                yield pref + nome, el, es

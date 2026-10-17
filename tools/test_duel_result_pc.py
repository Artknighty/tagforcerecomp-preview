#!/usr/bin/env python3
"""Testes de tools/duel_result_pc.py (funcoes puras e reescrita do textures.ini). Uso: python tools/test_duel_result_pc.py"""
import os
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import duel_result_pc as d


def rgba(w, h, cor, alfa=255):
    a = np.zeros((h, w, 4), np.uint8); a[..., :3] = cor; a[..., 3] = alfa
    return a


class Recolor(unittest.TestCase):
    def test_fundo_mapeia_os_quatro_tons_para_marinho(self):
        for g, esperado in [(206, d.NAVY_ESCURO), (250, d.NAVY_CLARO)]:
            o = d.fundo(rgba(2, 2, (255, g, 0)))
            self.assertTrue(np.allclose(o[0, 0, :3], esperado, atol=0.5))
            self.assertEqual(o[0, 0, 3], 255)

    def test_trechos_esticados_ficam_uniformes_e_o_resto_nao_muda(self):
        rng = np.random.default_rng(1)
        a = rng.integers(0, 256, (512, 2048, 4), dtype=np.uint8)
        o = d.uniformizar_esticados(a)
        e = d.ESCALA
        for u, v0, v1 in d.COLUNAS_ESTICADAS:   # coluna de 1 texel: os 8 pixels de cada linha iguais
            bloco = o[v0 * e:v1 * e, u * e:(u + 1) * e]
            self.assertTrue((bloco == bloco[:, :1]).all())
        for v, u0, u1 in d.LINHAS_ESTICADAS:    # linha de 1 texel: os 8 pixels de cada coluna iguais
            bloco = o[v * e:(v + 1) * e, u0 * e:u1 * e]
            self.assertTrue((bloco == bloco[:1]).all())
        self.assertTrue((o[:, 300 * 8 // 8:] == a[:, 300 * 8 // 8:]).all())   # longe dos trechos esticados, nada muda

    def test_xadrez_vira_casas_exatas_da_cor_do_centro(self):
        a = np.zeros((64, 64, 4), np.uint8); a[..., 3] = 255
        a[:32, :32, 1] = 206; a[:32, 32:, 1] = 250; a[32:, :32, 1] = 250; a[32:, 32:, 1] = 206
        a[:32, 30:34, 1] = 235                       # borda ondulada atravessando a divisa das casas
        o = d.endireitar_xadrez(a, casas=2)
        self.assertTrue((o[:32, :32, 1] == 206).all() and (o[:32, 32:, 1] == 250).all())
        self.assertTrue((o[32:, :32, 1] == 250).all() and (o[32:, 32:, 1] == 206).all())

    def test_alfa_sempre_preservado(self):
        a = rgba(4, 4, (200, 200, 200), alfa=77)
        for f in (d.fundo, d.titulo, d.painel, lambda x: d.oval(x, False), lambda x: d.oval(x, True)):
            self.assertTrue((f(a)[..., 3] == 77).all())

    def test_titulo_so_pinta_pixels_sem_cor(self):
        a = np.zeros((1, 2, 4), np.uint8); a[0, 0] = (230, 230, 230, 255); a[0, 1] = (255, 0, 0, 255)   # cinza e vermelho
        o = d.titulo(a)
        self.assertGreater(o[0, 0, 0], o[0, 0, 2])       # cinza -> dourado (R > B)
        self.assertTrue((o[0, 1, :3] == (255, 0, 0)).all())   # colorido fica

    def test_titulo_ignora_transparente(self):
        a = rgba(1, 1, (230, 230, 230), alfa=0)
        self.assertTrue((d.titulo(a)[0, 0, :3] == 230).all())

    def test_oval_transparente_e_so_marinho(self):
        o = d.oval(rgba(2, 2, (255, 255, 255), alfa=100), True)
        self.assertTrue(np.allclose(o[..., :3], d.NAVY_ESCURO))

    def test_oval_aro_claro_fica_dourado(self):
        o = d.oval(rgba(1, 1, (240, 240, 240)), False)[0, 0, :3]
        self.assertGreater(o[0], o[2])


class Painel(unittest.TestCase):
    def folha(self):   # 256x64 do PSP em 8x, tudo cinza medio (opaco)
        return rgba(256 * d.ESCALA, 64 * d.ESCALA, (60, 60, 60))

    def test_fora_da_regiao_nao_muda(self):
        a = self.folha(); o = d.painel(a)
        fora = (slice(0, 64 * d.ESCALA), slice(d.PAINEL_U * d.ESCALA, 256 * d.ESCALA))
        self.assertTrue((o[fora] == a[fora]).all())
        abaixo = (slice(d.PAINEL_V * d.ESCALA, 64 * d.ESCALA), slice(0, 256 * d.ESCALA))
        self.assertTrue((o[abaixo] == a[abaixo]).all())

    def test_cinza_escuro_vira_marinho_e_creme_vira_claro_azulado(self):
        a = self.folha(); a[8, 8, :3] = (254, 246, 222)
        o = d.painel(a)
        self.assertLess(o[100, 100, 0], 60)               # cinza escuro dentro da regiao
        self.assertGreater(o[100, 100, 2], o[100, 100, 0])   # com tom azulado
        self.assertGreater(o[8, 8, 0], 200)                # creme vira claro...
        self.assertGreater(o[8, 8, 2], o[8, 8, 0] - 5)     # ...sem o amarelado (B >= R aprox.)

    def test_pixel_transparente_nao_muda(self):
        a = self.folha(); a[5, 5] = (60, 60, 60, 0)
        self.assertTrue((d.painel(a)[5, 5, :3] == 60).all())


def leia(caminho):
    with open(caminho) as f:
        return f.read()


class Ini(unittest.TestCase):
    def test_gerar_e_idempotente_e_repontam_todas_as_chaves(self):
        with tempfile.TemporaryDirectory() as t:
            tex = os.path.join(t, "textures"); os.makedirs(os.path.join(tex, "mikeymk/ui/duels"))
            src = "mikeymk/ui/duels/results_background.png"
            from PIL import Image
            Image.fromarray(rgba(4, 4, (255, 212, 0))).save(os.path.join(tex, src))
            ini = os.path.join(tex, "textures.ini")
            with open(ini, "w") as f: f.write("[hashes]\n0000000000000000" "20b3dd72 = " + src + "\n0000000086303084" "20b3dd72 = " + src + "\n0000000000000000aaaaaaaa = outra.png\n")
            antigos = (d.TEX, d.INI, d.BAK, d.ALVOS)
            d.TEX, d.INI, d.BAK = tex, ini, ini + ".bak_resultado"
            d.ALVOS = {src: ("fundo", ["20b3dd72"])}
            try:
                d.gerar(); primeiro = leia(ini)
                d.gerar(); segundo = leia(ini)
            finally:
                d.TEX, d.INI, d.BAK, d.ALVOS = antigos
            self.assertEqual(primeiro, segundo)
            self.assertEqual(primeiro.count("duelresult_pc/results_background.png"), 2)
            self.assertIn("aaaaaaaa = outra.png", primeiro)
            self.assertTrue(os.path.exists(os.path.join(tex, "duelresult_pc", "results_background.png")))


if __name__ == "__main__":
    unittest.main(verbosity=2)

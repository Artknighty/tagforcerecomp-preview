"""Servidor falso com a API /api/generate do Ollama, para testar o decisor sem GPU: guarda cada prompt em gen/llm_mock_prompts.txt e responde sempre a opcao
indicada por --escolha (padrao 0). Uso: python tools/llm_mock.py [porta=11500] [--escolha N]"""
import sys, json
from http.server import BaseHTTPRequestHandler, HTTPServer
porta = int(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1].isdigit() else 11500
esc = int(sys.argv[sys.argv.index('--escolha') + 1]) if '--escolha' in sys.argv else 0
class H(BaseHTTPRequestHandler):
    def do_POST(self):
        n = int(self.headers.get('Content-Length', 0)); corpo = json.loads(self.rfile.read(n))
        with open('gen/llm_mock_prompts.txt', 'a', encoding='utf-8') as f: f.write('=' * 70 + '\n' + corpo.get('prompt', '') + '\n')
        r = json.dumps({'model': corpo.get('model'), 'response': json.dumps({'escolha': esc, 'motivo': 'mock'}), 'done': True}).encode()
        self.send_response(200); self.send_header('Content-Type', 'application/json'); self.send_header('Content-Length', str(len(r))); self.end_headers(); self.wfile.write(r)
    def log_message(self, *a): pass
HTTPServer(('127.0.0.1', porta), H).serve_forever()

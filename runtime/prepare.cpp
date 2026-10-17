/* First-run preparation of the release (generic) build. The release contains no game code: after the game files are installed from the user's own
 * ISO, the Allegrex -> C translator (tools/<name>.py, run by the embedded Python in tools/python) writes the C code of the game into gen/ on the
 * user's machine, and jit.cpp compiles it in memory with TinyCC. This file runs the translator and tells whether its output is up to date. */
#include "hle.h"
#include "util.h"
#include "gameid.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <string>
#include <vector>

static std::wstring compact_id(const std::string &id) { std::wstring w; for (char c : id) if (c != '-') w += (wchar_t)c; return w; }
static bool is_eu(const std::string &id) { return id.empty() || id == "ULES-00600"; }
static std::string read_text(const std::wstring &path) {
    std::string s; FILE *f = _wfopen(path.c_str(), L"rb"); if (!f) return s;
    char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n); fclose(f);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}
static bool file_exists(const std::wstring &p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }

std::wstring code_dir(const std::string &id) { return exe_dir() + L"gen\\" + (is_eu(id) ? L"" : compact_id(id) + L"\\"); }

/* the stamp ties the generated code to the translator that wrote it (tools/translator_version.txt) and to the game version */
static std::string stamp_want(const std::string &id) { return read_text(exe_dir() + L"tools\\translator_version.txt") + "|" + (id.empty() ? "ULES-00600" : id); }
bool code_ready(const std::string &id) {
    std::wstring d = code_dir(id);
    return file_exists(d + L"modulos.tsv") && read_text(d + L"translated.stamp") == stamp_want(id);
}

bool code_prepare(const std::string &id, const std::wstring &data, void (*cb)(double, const wchar_t *, void *), void *ctx, std::wstring *err) {
    auto fail = [&](const std::wstring &m) { if (err) *err = m; fprintf(stderr, "[prepare] %ls\n", m.c_str()); return false; };
    std::wstring tools = exe_dir() + L"tools\\", py = tools + L"python\\python.exe", script = tools + L"build_modules.py";
    if (!file_exists(py) || !file_exists(script)) return fail(L"The translator is missing (tools\\python, tools\\build_modules.py): extract the whole release zip.");
    std::wstring tsv = tools + (is_eu(id) ? L"modules.tsv" : L"modules_" + compact_id(id) + L".tsv");
    if (!file_exists(tsv)) return fail(L"This game version (" + to_wide(id) + L") is not supported by this release yet.");
    int total = 0;
    { std::string t = read_text(tsv); size_t p = 0; while (p < t.size()) { size_t e = t.find('\n', p); if (e == std::string::npos) e = t.size(); if (e > p && t[p] != '#') total++; p = e + 1; } }
    if (total <= 0) total = 1;
    SetEnvironmentVariableW(L"TF_JOGO", data.c_str());
    SetEnvironmentVariableW(L"TF_VER", is_eu(id) ? nullptr : compact_id(id).c_str());
    SetEnvironmentVariableW(L"TF_TOOLS", nullptr);
    SetEnvironmentVariableW(L"PYTHONIOENCODING", L"utf-8");
    DeleteFileW((code_dir(id) + L"translated.stamp").c_str());

    SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE}; HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return fail(L"Could not create a pipe for the translator.");
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si = {sizeof si}; si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = L"\"" + py + L"\" -u \"" + script + L"\"";
    std::vector<wchar_t> cl(cmd.begin(), cmd.end()); cl.push_back(0);
    BOOL ok = CreateProcessW(nullptr, cl.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, tools.c_str(), &si, &pi);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return fail(L"Could not start the translator."); }
    std::string acc, tail; int done = 0; char b[1024]; DWORD n;
    if (cb) cb(0.0, L"Translating the game code (first run only)...", ctx);
    while (ReadFile(rd, b, sizeof b, &n, nullptr) && n) {
        acc.append(b, n);
        size_t p;
        while ((p = acc.find('\n')) != std::string::npos) {
            std::string line = acc.substr(0, p); acc.erase(0, p + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            fprintf(stderr, "[prepare] %s\n", line.c_str());
            if (line.compare(0, 7, "recomp ") == 0) {
                std::wstring m = L"Translating " + to_wide(line.substr(7)) + L" (" + std::to_wstring(done + 1) + L"/" + std::to_wstring(total) + L")...";
                if (cb) cb((double)done / total, m.c_str(), ctx);
                done++;
            } else if (!line.empty()) { tail = line; }
        }
    }
    CloseHandle(rd);
    WaitForSingleObjectEx(pi.hProcess, INFINITE, FALSE);
    DWORD rc = 1; GetExitCodeProcess(pi.hProcess, &rc); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    if (rc != 0 || !file_exists(code_dir(id) + L"modulos.tsv")) return fail(L"The translator failed" + (tail.empty() ? std::wstring() : L": " + to_wide(tail)) + L" (see the log in logs\\).");
    std::string st = stamp_want(id); FILE *f = _wfopen((code_dir(id) + L"translated.stamp").c_str(), L"wb"); if (f) { fwrite(st.data(), 1, st.size(), f); fclose(f); }
    if (cb) cb(1.0, L"Done", ctx);
    return true;
}

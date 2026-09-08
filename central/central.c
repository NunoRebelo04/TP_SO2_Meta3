#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <io.h>
#include "utils.h"

DADOS_CENTRAL dadosth;

void escreve(DADOS_CENTRAL* dados, const TCHAR* texto);

PLACAR* procuraPlacarPorId(DADOS_CENTRAL* dados, DWORD id);
int indiceLivre(DADOS_CENTRAL* dados);

BOOL escreverPipe(PLACAR* p, const void* msg, DWORD tamanho);
BOOL lerPipe(HANDLE hPipe, void* buffer, DWORD tamanho, DWORD* lidos, HANDLE hEventoSair);
BOOL ligarPipeAsync(HANDLE hPipe, HANDLE hEventoSair);

void fecharPlacar(PLACAR* p);
void limparPlacarFinal(PLACAR* p);

void listar(DADOS_CENTRAL* dados);
void enviarAlerta(DADOS_CENTRAL* dados, const TCHAR* texto, DWORD duracao, DWORD id);
void cancelarAlerta(DADOS_CENTRAL* dados, DWORD id);
void encerrarCentral(DADOS_CENTRAL* dados);
void tratarComandoAlerta(DADOS_CENTRAL* dados, TCHAR* linha);

DWORD WINAPI threadPlacar(LPVOID lpParam);
DWORD WINAPI threadAceitaPlacares(LPVOID lpParam);
DWORD WINAPI threadComandos(LPVOID lpParam);

BOOL inicializarMemoriaMonitor(DADOS_CENTRAL* dados);
void atualizarMemoriaMonitor(DADOS_CENTRAL* dados);
void libertarMemoriaMonitor(DADOS_CENTRAL* dados);

BOOL inicializarMemoriaMonitor(DADOS_CENTRAL* dados) {
    dados->hMapAlertas = CreateFileMapping(
        INVALID_HANDLE_VALUE,
        NULL,
        PAGE_READWRITE,
        0,
        sizeof(SHM_ALERTA),
        NOME_MEMORIA_ALERTAS
    );

    if (dados->hMapAlertas == NULL) {
        return FALSE;
    }

    dados->shmAlertas = (SHM_ALERTA*)MapViewOfFile(
        dados->hMapAlertas,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        sizeof(SHM_ALERTA)
    );

    if (dados->shmAlertas == NULL) {
        CloseHandle(dados->hMapAlertas);
        dados->hMapAlertas = NULL;
        return FALSE;
    }

    dados->hEventoAlertas = CreateEvent(NULL, TRUE, FALSE, NOME_EVENTO_ALERTAS);

    if (dados->hEventoAlertas == NULL) {
        UnmapViewOfFile(dados->shmAlertas);
        CloseHandle(dados->hMapAlertas);
        dados->shmAlertas = NULL;
        dados->hMapAlertas = NULL;
        return FALSE;
    }

    ZeroMemory(dados->shmAlertas, sizeof(SHM_ALERTA));
    dados->shmAlertas->desligar = FALSE;

    SetEvent(dados->hEventoAlertas);

    return TRUE;
}

void atualizarMemoriaMonitor(DADOS_CENTRAL* dados) {
    int pos = 0;

    if (dados == NULL || dados->shmAlertas == NULL) {
        return;
    }

    ZeroMemory(dados->shmAlertas, sizeof(SHM_ALERTA));

    for (int i = 0; i < MAX_PLACARES && pos < MAX_PLACARES; i++) {
        PLACAR* p = &dados->placares[i];

        if (p->ocupado && p->alertaAtivo) {
            dados->shmAlertas->placar[pos].identificador = p->id;
            dados->shmAlertas->placar[pos].duracao = p->duracaoAtual;
            _tcsncpy_s(
                dados->shmAlertas->placar[pos].msg,
                TAM_MSG,
                p->msgAtual,
                _TRUNCATE
            );
            pos++;
        }
    }

    dados->shmAlertas->desligar = dados->terminar;

    if (dados->hEventoAlertas != NULL) {
        SetEvent(dados->hEventoAlertas);
    }
}

void libertarMemoriaMonitor(DADOS_CENTRAL* dados) {
    if (dados->shmAlertas != NULL) {
        UnmapViewOfFile(dados->shmAlertas);
        dados->shmAlertas = NULL;
    }

    if (dados->hMapAlertas != NULL) {
        CloseHandle(dados->hMapAlertas);
        dados->hMapAlertas = NULL;
    }

    if (dados->hEventoAlertas != NULL) {
        CloseHandle(dados->hEventoAlertas);
        dados->hEventoAlertas = NULL;
    }
}

void escreve(DADOS_CENTRAL* dados, const TCHAR* texto) {
    EnterCriticalSection(&dados->csEcra);

    if (texto != NULL && _tcslen(texto) > 0) {
        _tprintf(TEXT("\r%s\n"), texto);
    }

    _tprintf(TEXT("> "));
    fflush(stdout);

    LeaveCriticalSection(&dados->csEcra);
}

PLACAR* procuraPlacarPorId(DADOS_CENTRAL* dados, DWORD id) {
    for (int i = 0; i < MAX_PLACARES; i++) {
        if (dados->placares[i].ocupado && dados->placares[i].id == id) {
            return &dados->placares[i];
        }
    }

    return NULL;
}

int indiceLivre(DADOS_CENTRAL* dados) {
    for (int i = 0; i < MAX_PLACARES; i++) {
        if (!dados->placares[i].ocupado) {
            return i;
        }
    }

    return -1;
}

BOOL escreverPipe(PLACAR* p, const void* msg, DWORD tamanho) {
    DWORD escritos = 0;
    BOOL ok;

    if (p == NULL || p->hPipe == NULL || p->hPipe == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    EnterCriticalSection(&p->csPipe);
    ok = WriteFile(p->hPipe, msg, tamanho, &escritos, NULL);
    LeaveCriticalSection(&p->csPipe);

    return ok && escritos == tamanho;
}

BOOL lerPipe(HANDLE hPipe, void* buffer, DWORD tamanho, DWORD* lidos, HANDLE hEventoSair) {
    OVERLAPPED ov;
    HANDLE handles[2];
    BOOL ok;
    DWORD erro;

    ZeroMemory(&ov, sizeof(ov));
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (ov.hEvent == NULL) {
        return FALSE;
    }

    *lidos = 0;

    ok = ReadFile(hPipe, buffer, tamanho, NULL, &ov);

    if (!ok) {
        erro = GetLastError();

        if (erro == ERROR_IO_PENDING) {
            handles[0] = hEventoSair;
            handles[1] = ov.hEvent;

            DWORD r = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

            if (r == WAIT_OBJECT_0) {
                CancelIo(hPipe);
                CloseHandle(ov.hEvent);
                return FALSE;
            }

            ok = GetOverlappedResult(hPipe, &ov, lidos, FALSE);
        }
        else {
            CloseHandle(ov.hEvent);
            return FALSE;
        }
    }
    else {
        ok = GetOverlappedResult(hPipe, &ov, lidos, FALSE);
    }

    CloseHandle(ov.hEvent);

    return ok;
}

BOOL ligarPipeAsync(HANDLE hPipe, HANDLE hEventoSair) {
    OVERLAPPED ov;
    HANDLE handles[2];
    DWORD dummy = 0;
    BOOL ok;
    DWORD erro;
    DWORD r;

    ZeroMemory(&ov, sizeof(ov));
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (ov.hEvent == NULL) {
        return FALSE;
    }

    ok = ConnectNamedPipe(hPipe, &ov);

    if (ok) {
        CloseHandle(ov.hEvent);
        return TRUE;
    }

    erro = GetLastError();

    if (erro == ERROR_PIPE_CONNECTED) {
        CloseHandle(ov.hEvent);
        return TRUE;
    }

    if (erro != ERROR_IO_PENDING) {
        CloseHandle(ov.hEvent);
        return FALSE;
    }

    handles[0] = hEventoSair;
    handles[1] = ov.hEvent;

    r = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

    if (r == WAIT_OBJECT_0) {
        CancelIo(hPipe);
        CloseHandle(ov.hEvent);
        return FALSE;
    }

    ok = GetOverlappedResult(hPipe, &ov, &dummy, FALSE);

    CloseHandle(ov.hEvent);

    return ok;
}

void fecharPlacar(PLACAR* p) {
    if (p == NULL) {
        return;
    }

    if (p->hPipe != NULL && p->hPipe != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(p->hPipe);
        DisconnectNamedPipe(p->hPipe);
        CloseHandle(p->hPipe);
    }

    p->hPipe = INVALID_HANDLE_VALUE;
    p->hThread = NULL;
    p->remover = FALSE;
    p->ocupado = FALSE;
    p->id = 0;
    p->alertaAtivo = FALSE;
    p->msgAtual[0] = TEXT('\0');
    p->duracaoAtual = 0;
}

void limparPlacarFinal(PLACAR* p) {
    if (p == NULL) {
        return;
    }

    if (p->hThread != NULL) {
        CloseHandle(p->hThread);
    }

    DeleteCriticalSection(&p->csPipe);
    ZeroMemory(p, sizeof(PLACAR));
}

void listar(DADOS_CENTRAL* dados) {
    EnterCriticalSection(&dados->csEstado);
    EnterCriticalSection(&dados->csEcra);

    _tprintf(TEXT("--- PLACARES LIGADOS ---\n"));

    for (int i = 0; i < MAX_PLACARES; i++) {
        PLACAR* p = &dados->placares[i];

        if (p->ocupado) {
            _tprintf(TEXT("ID %lu"), p->id);

            if (p->alertaAtivo) {
                _tprintf(TEXT(" | ALERTA: '%s' (%lu SEGUNDOS)"),
                    p->msgAtual,
                    p->duracaoAtual
                );
            }
            else {
                _tprintf(TEXT(" | SEM ALERTA ATIVO"));
            }

            _tprintf(TEXT("\n"));
        }
    }

    _tprintf(TEXT("> "));
    fflush(stdout);

    LeaveCriticalSection(&dados->csEcra);
    LeaveCriticalSection(&dados->csEstado);
}

void enviarAlerta(DADOS_CENTRAL* dados, const TCHAR* texto, DWORD duracao, DWORD id) {
    MSG_ALERTA alerta;
    BOOL enviado = FALSE;

    ZeroMemory(&alerta, sizeof(alerta));
    alerta.tipo = TIPO_NOVO_ALERTA;
    _tcsncpy_s(alerta.msg, TAM_MSG, texto, _TRUNCATE);
    alerta.duracao = duracao;

    EnterCriticalSection(&dados->csEstado);

    for (int i = 0; i < MAX_PLACARES; i++) {
        PLACAR* p = &dados->placares[i];

        if (p->ocupado && (id == 0 || p->id == id)) {
            if (escreverPipe(p, &alerta, sizeof(alerta))) {
                enviado = TRUE;

                p->alertaAtivo = TRUE;
                _tcsncpy_s(p->msgAtual, TAM_MSG, texto, _TRUNCATE);
                p->duracaoAtual = duracao;

                atualizarMemoriaMonitor(dados);
            }
        }
    }

    LeaveCriticalSection(&dados->csEstado);

    if (!enviado) {
        escreve(dados, TEXT("NENHUM ALERTA ENVIADO. VERIFIQUE O ID DO PLACAR."));
    }
}

void cancelarAlerta(DADOS_CENTRAL* dados, DWORD id) {
    MSG_CMD cmd = { TIPO_CANCELAR };
    BOOL enviado = FALSE;

    EnterCriticalSection(&dados->csEstado);

    PLACAR* p = procuraPlacarPorId(dados, id);

    if (p == NULL || !p->ocupado) {
        LeaveCriticalSection(&dados->csEstado);
        escreve(dados, TEXT("PLACAR INEXISTENTE OU DESLIGADO."));
        return;
    }

    if (!p->alertaAtivo) {
        LeaveCriticalSection(&dados->csEstado);
        escreve(dados, TEXT("ESSE PLACAR NAO TEM ALERTA ATIVO."));
        return;
    }

    enviado = escreverPipe(p, &cmd, sizeof(cmd));

    if (enviado) {
        p->alertaAtivo = FALSE;
        p->msgAtual[0] = TEXT('\0');
        p->duracaoAtual = 0;

        atualizarMemoriaMonitor(dados);
    }

    LeaveCriticalSection(&dados->csEstado);

    if (!enviado) {
        escreve(dados, TEXT("NAO FOI POSSIVEL CANCELAR O ALERTA."));
    }
}

void encerrarCentral(DADOS_CENTRAL* dados) {
    MSG_CMD cmd = { TIPO_ENCERRAR };

    dados->terminar = TRUE;
    SetEvent(dados->hEventoSair);

    EnterCriticalSection(&dados->csEstado);

    for (int i = 0; i < MAX_PLACARES; i++) {
        PLACAR* p = &dados->placares[i];

        if (p->ocupado) {
            escreverPipe(p, &cmd, sizeof(cmd));
            p->remover = TRUE;
        }
    }

    atualizarMemoriaMonitor(dados);

    LeaveCriticalSection(&dados->csEstado);
}

void tratarComandoAlerta(DADOS_CENTRAL* dados, TCHAR* linha) {
    TCHAR* ptr = linha + 7;
    TCHAR* ultimo = _tcsrchr(ptr, TEXT(' '));
    TCHAR msg[TAM_MSG];
    DWORD duracao;
    DWORD id;

    if (ultimo == NULL) {
        escreve(dados, TEXT("USO: alerta <msg> <duracao> <id_placar>"));
        return;
    }

    id = (DWORD)_ttoi(ultimo + 1);
    *ultimo = TEXT('\0');

    ultimo = _tcsrchr(ptr, TEXT(' '));

    if (ultimo == NULL) {
        escreve(dados, TEXT("USO: alerta <msg> <duracao> <id_placar>"));
        return;
    }

    duracao = (DWORD)_ttoi(ultimo + 1);
    *ultimo = TEXT('\0');

    if (_tcslen(ptr) == 0 || duracao == 0) {
        escreve(dados, TEXT("MENSAGEM E DURACAO DEVEM SER VALIDAS."));
        return;
    }

    _tcsncpy_s(msg, TAM_MSG, ptr, _TRUNCATE);

    enviarAlerta(dados, msg, duracao, id);
}

DWORD WINAPI threadPlacar(LPVOID lpParam) {
    PLACAR* p = (PLACAR*)lpParam;
    DADOS_CENTRAL* dados = &dadosth;

    BYTE buffer[sizeof(MSG_ALERTA)];
    DWORD lidos;

    while (!dados->terminar && p->ocupado && !p->remover) {
        ZeroMemory(buffer, sizeof(buffer));

        if (!lerPipe(p->hPipe, buffer, sizeof(buffer), &lidos, dados->hEventoSair)) {
            if (!dados->terminar) {
                TCHAR texto[TAM_LINHA];
                _stprintf_s(texto, TAM_LINHA,
                    TEXT("LIGACAO AO PLACAR %lu PERDIDA."),
                    p->id
                );
                escreve(dados, texto);
            }

            fecharPlacar(p);

            EnterCriticalSection(&dados->csEstado);
            atualizarMemoriaMonitor(dados);
            LeaveCriticalSection(&dados->csEstado);

            break;
        }

        if (lidos < 1) {
            continue;
        }

        BYTE tipo = buffer[0];

        switch (tipo) {
        case TIPO_DESLIGAR: {
            MSG_CMD resposta = { TIPO_DESLIGAR };
            escreverPipe(p, &resposta, sizeof(resposta));

            TCHAR texto[TAM_LINHA];
            _stprintf_s(texto, TAM_LINHA,
                TEXT("PLACAR %lu DESLIGOU."),
                p->id
            );
            escreve(dados, texto);

            fecharPlacar(p);

            EnterCriticalSection(&dados->csEstado);
            atualizarMemoriaMonitor(dados);
            LeaveCriticalSection(&dados->csEstado);

            return 0;
        }

        case TIPO_FIM_ALERTA: {
            EnterCriticalSection(&dados->csEstado);

            if (p->alertaAtivo) {
                p->alertaAtivo = FALSE;
                p->msgAtual[0] = TEXT('\0');
                p->duracaoAtual = 0;

                atualizarMemoriaMonitor(dados);

                LeaveCriticalSection(&dados->csEstado);

                TCHAR texto[TAM_LINHA];
                _stprintf_s(texto, TAM_LINHA,
                    TEXT("PLACAR %lu TERMINOU O ALERTA."),
                    p->id
                );
                escreve(dados, texto);
            }
            else {
                LeaveCriticalSection(&dados->csEstado);
            }

            break;
        }

        case TIPO_NOVO_ALERTA: {
            if (lidos == sizeof(MSG_ALERTA)) {
                MSG_ALERTA* ack = (MSG_ALERTA*)buffer;

                TCHAR texto[TAM_LINHA];
                _stprintf_s(texto, TAM_LINHA,
                    TEXT("CONFIRMACAO DE ALERTA DO PLACAR %lu: '%s'"),
                    p->id,
                    ack->msg
                );
                escreve(dados, texto);
            }

            break;
        }

        case TIPO_CANCELAR: {
            TCHAR texto[TAM_LINHA];
            _stprintf_s(texto, TAM_LINHA,
                TEXT("CONFIRMACAO DE CANCELAMENTO DO PLACAR %lu."),
                p->id
            );
            escreve(dados, texto);

            break;
        }

        default:
            break;
        }
    }

    return 0;
}

DWORD WINAPI threadAceitaPlacares(LPVOID lpParam) {
    DADOS_CENTRAL* dados = (DADOS_CENTRAL*)lpParam;

    while (!dados->terminar) {
        HANDLE hPipe = CreateNamedPipe(
            dados->caminhoPipe,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            MAX_INSTANCIAS_PIPE,
            sizeof(MSG_ALERTA),
            sizeof(MSG_ALERTA),
            0,
            NULL
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            Sleep(500);
            continue;
        }

        BOOL ligado = ligarPipeAsync(hPipe, dados->hEventoSair);

        if (!ligado) {
            CloseHandle(hPipe);
            continue;
        }

        if (dados->terminar) {
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            break;
        }

        BYTE buffer[sizeof(MSG_ALERTA)];
        DWORD lidos;

        ZeroMemory(buffer, sizeof(buffer));

        if (!lerPipe(hPipe, buffer, sizeof(buffer), &lidos, dados->hEventoSair)
            || buffer[0] != TIPO_LIGAR) {
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            continue;
        }

        EnterCriticalSection(&dados->csEstado);

        int idx = indiceLivre(dados);

        if (idx < 0) {
            MSG_CMD resposta;
            DWORD escritos;

            resposta.tipo = TIPO_DESLIGAR;

            LeaveCriticalSection(&dados->csEstado);

            WriteFile(hPipe, &resposta, sizeof(resposta), &escritos, NULL);

            FlushFileBuffers(hPipe);
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);

            escreve(dados, TEXT("PLACAR REJEITADO: LIMITE MAXIMO ATINGIDO."));

            continue;
        }

        DWORD novoId = dados->proximoId++;

        PLACAR* p = &dados->placares[idx];

        ZeroMemory(p, sizeof(PLACAR));

        p->ocupado = TRUE;
        p->remover = FALSE;
        p->id = novoId;
        p->hPipe = hPipe;
        p->alertaAtivo = FALSE;
        p->duracaoAtual = 0;

        InitializeCriticalSection(&p->csPipe);

        MSG_ID resposta = { TIPO_ID, novoId };

        escreverPipe(p, &resposta, sizeof(resposta));

        atualizarMemoriaMonitor(dados);

        p->hThread = CreateThread(NULL, 0, threadPlacar, p, 0, NULL);

        LeaveCriticalSection(&dados->csEstado);

        TCHAR texto[TAM_LINHA];
        _stprintf_s(texto, TAM_LINHA,
            TEXT("NOVO PLACAR LIGADO. IDENTIFICADOR = %lu"),
            novoId
        );
        escreve(dados, texto);
    }

    return 0;
}

DWORD WINAPI threadComandos(LPVOID lpParam) {
    DADOS_CENTRAL* dados = (DADOS_CENTRAL*)lpParam;
    TCHAR linha[TAM_LINHA];

    while (!dados->terminar) {
        escreve(dados, NULL);

        if (_fgetts(linha, TAM_LINHA, stdin) == NULL) {
            encerrarCentral(dados);
            break;
        }

        linha[_tcscspn(linha, TEXT("\r\n"))] = TEXT('\0');

        if (_tcsncmp(linha, TEXT("alerta "), 7) == 0) {
            tratarComandoAlerta(dados, linha);
        }
        else if (_tcsncmp(linha, TEXT("cancelar "), 9) == 0) {
            cancelarAlerta(dados, (DWORD)_ttoi(linha + 9));
        }
        else if (_tcscmp(linha, TEXT("listar")) == 0) {
            listar(dados);
        }
        else if (_tcscmp(linha, TEXT("encerrar")) == 0) {
            encerrarCentral(dados);
            break;
        }
        else if (_tcslen(linha) > 0) {
            escreve(dados,
                TEXT("COMANDOS: alerta <msg> <duracao> <id>, cancelar <id>, listar, encerrar")
            );
        }
    }

    return 0;
}

int _tmain(int argc, TCHAR* argv[]) {
    HANDLE hMutexCentral;
    HANDLE hAceita;
    HANDLE hCmd;

#ifdef UNICODE
    _setmode(_fileno(stdin), _O_WTEXT);
    _setmode(_fileno(stdout), _O_WTEXT);
    _setmode(_fileno(stderr), _O_WTEXT);
#endif

    hMutexCentral = CreateMutex(NULL, TRUE, TEXT("MUTEX_CENTRAL"));

    if (hMutexCentral == NULL) {
        _tprintf(TEXT("ERRO AO CRIAR MUTEX DO CENTRAL.\n"));
        return 1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        _tprintf(TEXT("ERRO: JA EXISTE UMA INSTANCIA DO CENTRAL A CORRER.\n"));
        CloseHandle(hMutexCentral);
        return 1;
    }

    if (argc != 2) {
        _tprintf(TEXT("USO: central.exe <nome_pipe>\n"));
        ReleaseMutex(hMutexCentral);
        CloseHandle(hMutexCentral);
        return 1;
    }

    ZeroMemory(&dadosth, sizeof(dadosth));

    for (int i = 0; i < MAX_PLACARES; i++) {
        dadosth.placares[i].ocupado = FALSE;
        dadosth.placares[i].remover = FALSE;
        dadosth.placares[i].id = 0;
        dadosth.placares[i].hPipe = INVALID_HANDLE_VALUE;
        dadosth.placares[i].hThread = NULL;
        dadosth.placares[i].alertaAtivo = FALSE;
        dadosth.placares[i].duracaoAtual = 0;
        dadosth.placares[i].msgAtual[0] = TEXT('\0');
    }

    _tcsncpy_s(dadosth.nomePipe, TAM_PIPE, argv[1], _TRUNCATE);
    _stprintf_s(dadosth.caminhoPipe, TAM_PIPE,
        TEXT("\\\\.\\pipe\\%s"),
        dadosth.nomePipe
    );

    dadosth.proximoId = 1;
    dadosth.terminar = FALSE;
    dadosth.hEventoSair = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (!inicializarMemoriaMonitor(&dadosth)) {
        _tprintf(TEXT("ERRO AO CRIAR MEMORIA PARTILHADA DO MONITOR.\n"));

        if (dadosth.hEventoSair != NULL) {
            CloseHandle(dadosth.hEventoSair);
        }

        ReleaseMutex(hMutexCentral);
        CloseHandle(hMutexCentral);

        return 1;
    }

    InitializeCriticalSection(&dadosth.csEstado);
    InitializeCriticalSection(&dadosth.csEcra);

    _tprintf(TEXT("CENTRAL ATIVA NO NAMED PIPE '%s'\n"), dadosth.caminhoPipe);

    hAceita = CreateThread(NULL, 0, threadAceitaPlacares, &dadosth, 0, NULL);
    hCmd = CreateThread(NULL, 0, threadComandos, &dadosth, 0, NULL);

    if (hAceita == NULL || hCmd == NULL) {
        _tprintf(TEXT("ERRO AO CRIAR THREADS DO CENTRAL.\n"));

        dadosth.terminar = TRUE;

        if (hAceita != NULL) {
            CloseHandle(hAceita);
        }

        if (hCmd != NULL) {
            CloseHandle(hCmd);
        }

        CloseHandle(dadosth.hEventoSair);
        libertarMemoriaMonitor(&dadosth);

        DeleteCriticalSection(&dadosth.csEstado);
        DeleteCriticalSection(&dadosth.csEcra);

        ReleaseMutex(hMutexCentral);
        CloseHandle(hMutexCentral);

        return 1;
    }

    WaitForSingleObject(hCmd, INFINITE);

    dadosth.terminar = TRUE;
    SetEvent(dadosth.hEventoSair);

    WaitForSingleObject(hAceita, INFINITE);

    EnterCriticalSection(&dadosth.csEstado);

    for (int i = 0; i < MAX_PLACARES; i++) {
        if (dadosth.placares[i].ocupado || dadosth.placares[i].remover) {
            fecharPlacar(&dadosth.placares[i]);
        }
    }

    atualizarMemoriaMonitor(&dadosth);

    LeaveCriticalSection(&dadosth.csEstado);

    for (int i = 0; i < MAX_PLACARES; i++) {
        if (dadosth.placares[i].hThread != NULL) {
            WaitForSingleObject(dadosth.placares[i].hThread, 1000);
            limparPlacarFinal(&dadosth.placares[i]);
        }
    }

    CloseHandle(hCmd);
    CloseHandle(hAceita);
    CloseHandle(dadosth.hEventoSair);

    libertarMemoriaMonitor(&dadosth);

    DeleteCriticalSection(&dadosth.csEstado);
    DeleteCriticalSection(&dadosth.csEcra);

    ReleaseMutex(hMutexCentral);
    CloseHandle(hMutexCentral);

    return 0;
}
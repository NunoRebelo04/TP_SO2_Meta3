#include "utils.h"


void mostraMensagem(DADOS_PLACAR* dados, const TCHAR* texto);
void mostraMensagemComHora(DADOS_PLACAR* dados, const TCHAR* texto);

int guardarPipeNoRegistry(const TCHAR* nomePipe);
int lerPipeDoRegistry(TCHAR* nomePipe, DWORD tam);

BOOL escreverPipe(DADOS_PLACAR* dados, const void* msg, DWORD tamanho);
BOOL lerPipe(DADOS_PLACAR* dados, void* buffer, DWORD tamanho, DWORD* lidos);
BOOL ligarAoCentral(DADOS_PLACAR* dados);

int inicializaPlacar(DADOS_PLACAR* dados, int argc, TCHAR* argv[]);
void libertaPlacar(DADOS_PLACAR* dados);

BOOL iniciarLigacao(DADOS_PLACAR* dados);

DWORD WINAPI threadRecebeCentral(LPVOID lpParam);
DWORD WINAPI threadTimer(LPVOID lpParam);
DWORD WINAPI threadComandos(LPVOID lpParam);


void mostraMensagem(DADOS_PLACAR* dados, const TCHAR* texto) {
    EnterCriticalSection(&dados->csEcra);
    _tprintf(TEXT("%s\n"), texto);
    LeaveCriticalSection(&dados->csEcra);
}

void mostraMensagemComHora(DADOS_PLACAR* dados, const TCHAR* texto) {
    SYSTEMTIME st;
    GetLocalTime(&st);

    EnterCriticalSection(&dados->csEcra);
    _tprintf(TEXT("%02d/%02d/%04d (%02d:%02d:%02d): '%s'\n"),
        st.wDay, st.wMonth, st.wYear,
        st.wHour, st.wMinute, st.wSecond,
        texto);
    LeaveCriticalSection(&dados->csEcra);
}

int guardarPipeNoRegistry(const TCHAR* nomePipe) {
    HKEY chave;
    LONG resultado;

    resultado = RegCreateKeyEx(
        HKEY_CURRENT_USER,
        CHAVE_REGISTO,
        0,
        NULL,
        REG_OPTION_NON_VOLATILE,
        KEY_ALL_ACCESS,
        NULL,
        &chave,
        NULL
    );

    if (resultado != ERROR_SUCCESS) {
        return 0;
    }

    resultado = RegSetValueEx(
        chave,
        VALOR_NPIPE,
        0,
        REG_SZ,
        (const BYTE*)nomePipe,
        ((DWORD)_tcslen(nomePipe) + 1) * sizeof(TCHAR)
    );

    RegCloseKey(chave);

    return resultado == ERROR_SUCCESS;
}

int lerPipeDoRegistry(TCHAR* nomePipe, DWORD tam) {
    HKEY chave;
    DWORD tipo = 0;
    DWORD tamanhoBytes = tam * sizeof(TCHAR);
    LONG resultado;

    resultado = RegOpenKeyEx(
        HKEY_CURRENT_USER,
        CHAVE_REGISTO,
        0,
        KEY_READ,
        &chave
    );

    if (resultado != ERROR_SUCCESS) {
        return 0;
    }

    resultado = RegQueryValueEx(
        chave,
        VALOR_NPIPE,
        NULL,
        &tipo,
        (LPBYTE)nomePipe,
        &tamanhoBytes
    );

    RegCloseKey(chave);

    return resultado == ERROR_SUCCESS && tipo == REG_SZ;
}

BOOL escreverPipe(DADOS_PLACAR* dados, const void* msg, DWORD tamanho) {
    OVERLAPPED ov;
    DWORD escritos = 0;
    BOOL ok;
    DWORD erro;

    if (dados->hPipe == NULL || dados->hPipe == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    ZeroMemory(&ov, sizeof(ov));
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (ov.hEvent == NULL) {
        return FALSE;
    }

    EnterCriticalSection(&dados->csPipe);

    ok = WriteFile(dados->hPipe, msg, tamanho, NULL, &ov);

    if (!ok) {
        erro = GetLastError();

        if (erro == ERROR_IO_PENDING) {
            ok = GetOverlappedResult(dados->hPipe, &ov, &escritos, TRUE);
        }
        else {
            escritos = 0;
        }
    }
    else {
        escritos = tamanho;
    }

    LeaveCriticalSection(&dados->csPipe);

    CloseHandle(ov.hEvent);

    return ok && escritos == tamanho;
}

BOOL lerPipe(DADOS_PLACAR* dados, void* buffer, DWORD tamanho, DWORD* lidos) {
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

    ok = ReadFile(dados->hPipe, buffer, tamanho, NULL, &ov);

    if (!ok) {
        erro = GetLastError();

        if (erro == ERROR_IO_PENDING) {
            handles[0] = dados->hEventoSair;
            handles[1] = ov.hEvent;

            DWORD r = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

            if (r == WAIT_OBJECT_0) {
                CancelIo(dados->hPipe);
                CloseHandle(ov.hEvent);
                return FALSE;
            }

            ok = GetOverlappedResult(dados->hPipe, &ov, lidos, FALSE);
        }
        else {
            CloseHandle(ov.hEvent);
            return FALSE;
        }
    }
    else {
        ok = GetOverlappedResult(dados->hPipe, &ov, lidos, FALSE);
    }

    CloseHandle(ov.hEvent);
    return ok;
}

BOOL ligarAoCentral(DADOS_PLACAR* dados) {
    DWORD modo;

    dados->hPipe = CreateFile(
        dados->caminhoPipe,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        NULL
    );

    if (dados->hPipe == INVALID_HANDLE_VALUE) {
        mostraMensagem(dados, TEXT("ERRO: NAO FOI POSSIVEL AO CENTRAL."));
        dados->hPipe = NULL;
        return FALSE;
    }

    modo = PIPE_READMODE_MESSAGE;

    if (!SetNamedPipeHandleState(dados->hPipe, &modo, NULL, NULL)) {
        mostraMensagem(dados, TEXT("ERRO: NAO FOI POSSIVEL CONFIGURAR O PIPE."));
        CloseHandle(dados->hPipe);
        dados->hPipe = NULL;
        return FALSE;
    }

    return TRUE;
}

int inicializaPlacar(DADOS_PLACAR* dados, int argc, TCHAR* argv[]) {
    ZeroMemory(dados, sizeof(DADOS_PLACAR));

    dados->hPipe = NULL;
    dados->ligado = FALSE;
    dados->timerAtivo = FALSE;

    if (!InitializeCriticalSectionAndSpinCount(&dados->csEcra, 1)) {
        return 0;
    }

    if (!InitializeCriticalSectionAndSpinCount(&dados->csPipe, 1)) {
        DeleteCriticalSection(&dados->csEcra);
        return 0;
    }
    
    if (!InitializeCriticalSectionAndSpinCount(&dados->csEstado, 1)) {
        DeleteCriticalSection(&dados->csPipe);
        DeleteCriticalSection(&dados->csEcra);
        return 0;
    }

    if (argc >= 2) {
        _tcsncpy_s(dados->nomePipe, TAM, argv[1], _TRUNCATE);

        if (!guardarPipeNoRegistry(dados->nomePipe)) {
            DeleteCriticalSection(&dados->csEstado);
            DeleteCriticalSection(&dados->csPipe);
            DeleteCriticalSection(&dados->csEcra);
            return 0;
        }
    }
    else {
        if (!lerPipeDoRegistry(dados->nomePipe, TAM)) {
            DeleteCriticalSection(&dados->csEstado);
            DeleteCriticalSection(&dados->csPipe);
            DeleteCriticalSection(&dados->csEcra);
            _tprintf(TEXT("[ERRO] O NOME DO 'NPIPE' NAO FOI ESPECIFICADO.\n"));
            return 0;
        }
    }

    _stprintf_s(dados->caminhoPipe, TAM, TEXT("\\\\.\\pipe\\%s"), dados->nomePipe);

    dados->hEventoSair = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (dados->hEventoSair == NULL) {
        DeleteCriticalSection(&dados->csEstado);
        DeleteCriticalSection(&dados->csPipe);
        DeleteCriticalSection(&dados->csEcra);
        return 0;
    }

    dados->hTimer = CreateWaitableTimer(NULL, FALSE, NULL);

    if (dados->hTimer == NULL) {
        CloseHandle(dados->hEventoSair);
        DeleteCriticalSection(&dados->csEstado);
        DeleteCriticalSection(&dados->csPipe);
        DeleteCriticalSection(&dados->csEcra);
        return 0;
    }

    return 1;
}

void libertaPlacar(DADOS_PLACAR* dados) {
    if (dados->hTimer != NULL) {
        CancelWaitableTimer(dados->hTimer);
        CloseHandle(dados->hTimer);
        dados->hTimer = NULL;
    }

    if (dados->hPipe != NULL && dados->hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(dados->hPipe);
        dados->hPipe = NULL;
    }

    if (dados->hEventoSair != NULL) {
        CloseHandle(dados->hEventoSair);
        dados->hEventoSair = NULL;
    }

    DeleteCriticalSection(&dados->csPipe);
    DeleteCriticalSection(&dados->csEcra);
    DeleteCriticalSection(&dados->csEstado);
}

BOOL iniciarLigacao(DADOS_PLACAR* dados) {
    MSG_CMD cmd = { TIPO_LIGAR };

    BOOL jaLigado;

    EnterCriticalSection(&dados->csEstado);
    jaLigado = dados->ligado || dados->hPipe != NULL;
    LeaveCriticalSection(&dados->csEstado);

    if (jaLigado) {
        mostraMensagem(dados, TEXT("O PLACAR JA ESTA LIGADO OU A AGUARDAR IDENTIFICADOR."));
        return TRUE;
    }

    if (!ligarAoCentral(dados)) {
        return FALSE;
    }

    if (!escreverPipe(dados, &cmd, sizeof(cmd))) {
        mostraMensagem(dados, TEXT("ERRO: NAO FOI ENVIAR PEDIDO DE LIGACAO."));
        CloseHandle(dados->hPipe);
        dados->hPipe = NULL;
        return FALSE;
    }

    dados->hThreadRecebe = CreateThread(NULL, 0, threadRecebeCentral, dados, 0, NULL);
    dados->hThreadTimer = CreateThread(NULL, 0, threadTimer, dados, 0, NULL);

    if (dados->hThreadRecebe == NULL || dados->hThreadTimer == NULL) {
        mostraMensagem(dados, TEXT("ERRO: NAO FOI POSSIVEL CRIAR THREADS DO PLACAR."));
        SetEvent(dados->hEventoSair);
        return FALSE;
    }

    return TRUE;
}

DWORD WINAPI threadRecebeCentral(LPVOID lpParam) {
    DADOS_PLACAR* dados = (DADOS_PLACAR*)lpParam;
    BYTE buffer[sizeof(MSG_ALERTA)];
    DWORD lidos;

    while (WaitForSingleObject(dados->hEventoSair, 0) == WAIT_TIMEOUT) {
        ZeroMemory(buffer, sizeof(buffer));

        if (!lerPipe(dados, buffer, sizeof(buffer), &lidos)) {
            SetEvent(dados->hEventoSair);
            break;
        }

        if (lidos < 1) {
            continue;
        }

        BYTE tipo = buffer[0];

        switch (tipo) {
        case TIPO_ID:
            if (lidos == sizeof(MSG_ID)) {
                MSG_ID* msgId = (MSG_ID*)buffer;

                EnterCriticalSection(&dados->csEstado);
                dados->identificador = msgId->identificador;
                dados->ligado = TRUE;
                LeaveCriticalSection(&dados->csEstado);

                EnterCriticalSection(&dados->csEcra);
                _tprintf(TEXT("IDENTIFICADOR = %lu\n"), dados->identificador);
                LeaveCriticalSection(&dados->csEcra);
            }
            break;

        case TIPO_NOVO_ALERTA:
            if (lidos == sizeof(MSG_ALERTA)) {
                MSG_ALERTA* alerta = (MSG_ALERTA*)buffer;

                escreverPipe(dados, alerta, sizeof(MSG_ALERTA));

                EnterCriticalSection(&dados->csEstado);

                if (dados->timerAtivo) {
                    CancelWaitableTimer(dados->hTimer);
                    dados->timerAtivo = FALSE;
                }

                LeaveCriticalSection(&dados->csEstado);

                mostraMensagemComHora(dados, alerta->msg);

                LARGE_INTEGER tempo;
                tempo.QuadPart = -((LONGLONG)alerta->duracao * 10000000LL);

                if (SetWaitableTimer(dados->hTimer, &tempo, 0, NULL, NULL, FALSE)) {
                    EnterCriticalSection(&dados->csEstado);
                    dados->timerAtivo = TRUE;
                    LeaveCriticalSection(&dados->csEstado);
                }
                else {
                    mostraMensagem(dados, TEXT("ERRO A ATIVAR O WAITABLE TIMER."));
                }
            }
            break;

        case TIPO_CANCELAR: {
            MSG_CMD resposta = { TIPO_CANCELAR };

            escreverPipe(dados, &resposta, sizeof(resposta));

            EnterCriticalSection(&dados->csEstado);

            if (dados->timerAtivo) {
                CancelWaitableTimer(dados->hTimer);
                dados->timerAtivo = FALSE;
            }

            LeaveCriticalSection(&dados->csEstado);

            mostraMensagemComHora(dados, TEXT("---"));
            break;
        }

        case TIPO_DESLIGAR:
            mostraMensagem(dados, TEXT("LIGACAO TERMINADA PELO CENTRAL."));
            EnterCriticalSection(&dados->csEstado);
            dados->ligado = FALSE;
            dados->timerAtivo = FALSE;
            LeaveCriticalSection(&dados->csEstado);
            SetEvent(dados->hEventoSair);
            break;

        case TIPO_ENCERRAR:
            mostraMensagem(dados, TEXT("CENTRAL ENCERROU A PLATAFORMA."));
            EnterCriticalSection(&dados->csEstado);
            dados->ligado = FALSE;
            dados->timerAtivo = FALSE;
            LeaveCriticalSection(&dados->csEstado);

            SetEvent(dados->hEventoSair);
            break;

        default:
            break;
        }
    }

    return 0;
}

DWORD WINAPI threadTimer(LPVOID lpParam) {
    DADOS_PLACAR* dados = (DADOS_PLACAR*)lpParam;
    HANDLE handles[2];

    handles[0] = dados->hEventoSair;
    handles[1] = dados->hTimer;

    while (1) {
        DWORD resultado = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

        if (resultado == WAIT_OBJECT_0) {
            break;
        }

        if (resultado == WAIT_OBJECT_0 + 1) {
            BOOL enviarFim = FALSE;

            EnterCriticalSection(&dados->csEstado);

            if (dados->timerAtivo) {
                dados->timerAtivo = FALSE;
                enviarFim = dados->ligado;
            }

            LeaveCriticalSection(&dados->csEstado);

            if (enviarFim) {
                mostraMensagemComHora(dados, TEXT("---"));

                MSG_CMD fim = { TIPO_FIM_ALERTA };
                escreverPipe(dados, &fim, sizeof(fim));
            }
        }
    }

    return 0;
}

DWORD WINAPI threadComandos(LPVOID lpParam) {
    DADOS_PLACAR* dados = (DADOS_PLACAR*)lpParam;
    TCHAR comando[80];

    while (WaitForSingleObject(dados->hEventoSair, 0) == WAIT_TIMEOUT) {
        EnterCriticalSection(&dados->csEcra);
        _tprintf(TEXT("> "));
        fflush(stdout);
        LeaveCriticalSection(&dados->csEcra);

        if (_fgetts(comando, 80, stdin) == NULL) {
            SetEvent(dados->hEventoSair);
            break;
        }

        comando[_tcscspn(comando, TEXT("\r\n"))] = TEXT('\0');

        if (_tcscmp(comando, TEXT("ligar")) == 0) {
            iniciarLigacao(dados);
        }
        else if (_tcscmp(comando, TEXT("desligar")) == 0) {
            BOOL podeDesligar;

            EnterCriticalSection(&dados->csEstado);
            podeDesligar = dados->ligado && dados->hPipe != NULL && dados->hPipe != INVALID_HANDLE_VALUE;
            LeaveCriticalSection(&dados->csEstado);

            if (podeDesligar) {
                MSG_CMD cmd = { TIPO_DESLIGAR };

                if (escreverPipe(dados, &cmd, sizeof(cmd))) {
                    mostraMensagem(dados, TEXT("PEDIDO DE ENCERRAMENTO ENVIADO AO CENTRAL."));
                }
                else {
                    mostraMensagem(dados, TEXT("ERRO AO ENVIAR PEDIDO DE ENCERRAMENTO AO CENTRAL."));
                    SetEvent(dados->hEventoSair);
                }
            }
            else {
                SetEvent(dados->hEventoSair);
            }

            break;
        }
        else if (_tcslen(comando) == 0) {
            /* não faz nada */
        }
        else {
            mostraMensagem(dados, TEXT("COMANDO INVALIDO: USE 'LIGAR' OU 'DESLIGAR'"));
        }
    }

    return 0;
}

int _tmain(int argc, TCHAR* argv[]) {
    DADOS_PLACAR dados;
    HANDLE hThreadComandos;

#ifdef UNICODE
    _setmode(_fileno(stdin), _O_WTEXT);
    _setmode(_fileno(stdout), _O_WTEXT);
    _setmode(_fileno(stderr), _O_WTEXT);
#endif

    if (!inicializaPlacar(&dados, argc, argv)) {
        return 1;
    }

    _tprintf(TEXT("NamedPipe = '%s'\n"), dados.nomePipe);

    if (!WaitNamedPipe(dados.caminhoPipe, 1000)) {
        _tprintf(TEXT("ERRO: O CENTRAL NAO ESTA DISPONIVEL OU NAO HA VAGAS PARA MAIS PLACARES.\n"));
        libertaPlacar(&dados);
        return 1;
    }

    hThreadComandos = CreateThread(NULL, 0, threadComandos, &dados, 0, NULL);

    if (hThreadComandos == NULL) {
        _tprintf(TEXT("ERRO AO CRIAR A THREAD DE COMANDOS.\n"));
        libertaPlacar(&dados);
        return 1;
    }

    WaitForSingleObject(dados.hEventoSair, INFINITE);

    CancelWaitableTimer(dados.hTimer);

    if (dados.hPipe != NULL && dados.hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(dados.hPipe);
        dados.hPipe = NULL;
    }

    WaitForSingleObject(hThreadComandos, 1000);
    CloseHandle(hThreadComandos);

    if (dados.hThreadRecebe != NULL) {
        WaitForSingleObject(dados.hThreadRecebe, 1000);
        CloseHandle(dados.hThreadRecebe);
    }

    if (dados.hThreadTimer != NULL) {
        WaitForSingleObject(dados.hThreadTimer, 1000);
        CloseHandle(dados.hThreadTimer);
    }

    libertaPlacar(&dados);

    return 0;
}
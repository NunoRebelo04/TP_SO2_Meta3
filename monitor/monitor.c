#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include "utils.h"
#include "resource.h"

#define WM_ATUALIZAR_ALERTAS (WM_APP + 1)
#define TAM_NOME_RECURSO 100

TCHAR szProgName[] = TEXT("MonitorAlertasSO2");

typedef struct {
    HANDLE hMap;
    HANDLE hEvento;
    HANDLE hEventoSair;
    HANDLE hThread;

    SHM_ALERTA* shm;
    SHM_ALERTA copia;

    HANDLE hMutex;

    BOOL centralDisponivel;

    int maxAlertasPagina;
    int paginaAtual;

    TCHAR nomeMemoria[TAM_NOME_RECURSO];
    TCHAR nomeEvento[TAM_NOME_RECURSO];

} DADOS_MONITOR;

LRESULT CALLBACK trataEventos(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK DlgConfig(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);

DWORD WINAPI threadNotificacoes(LPVOID lpParam);

BOOL abrirMemoriaCentral(DADOS_MONITOR* dados);
void fecharMemoriaCentral(DADOS_MONITOR* dados);
void desenharMonitor(HWND hwnd, DADOS_MONITOR* dados);
int contarAlertasAtivos(DADOS_MONITOR* dados);

BOOL abrirMemoriaCentral(DADOS_MONITOR* dados) {
    fecharMemoriaCentral(dados);

    dados->hMap = OpenFileMapping(FILE_MAP_READ, FALSE, dados->nomeMemoria);

    if (dados->hMap == NULL) {
        dados->centralDisponivel = FALSE;
        return FALSE;
    }

    dados->shm = (SHM_ALERTA*)MapViewOfFile(
        dados->hMap,
        FILE_MAP_READ,
        0,
        0,
        sizeof(SHM_ALERTA)
    );

    if (dados->shm == NULL) {
        CloseHandle(dados->hMap);
        dados->hMap = NULL;
        dados->centralDisponivel = FALSE;
        return FALSE;
    }

    dados->hEvento = OpenEvent(EVENT_ALL_ACCESS, FALSE, dados->nomeEvento);

    if (dados->hEvento == NULL) {
        UnmapViewOfFile(dados->shm);
        CloseHandle(dados->hMap);

        dados->shm = NULL;
        dados->hMap = NULL;
        dados->centralDisponivel = FALSE;

        return FALSE;
    }

    WaitForSingleObject(dados->hMutex, INFINITE);
    CopyMemory(&dados->copia, dados->shm, sizeof(SHM_ALERTA));
    dados->centralDisponivel = TRUE;
    ReleaseMutex(dados->hMutex);

    return TRUE;
}

void fecharMemoriaCentral(DADOS_MONITOR* dados) {
    if (dados->shm != NULL) {
        UnmapViewOfFile(dados->shm);
        dados->shm = NULL;
    }

    if (dados->hMap != NULL) {
        CloseHandle(dados->hMap);
        dados->hMap = NULL;
    }

    if (dados->hEvento != NULL) {
        CloseHandle(dados->hEvento);
        dados->hEvento = NULL;
    }

    dados->centralDisponivel = FALSE;
}

int contarAlertasAtivos(DADOS_MONITOR* dados) {
    int total = 0;

    for (int i = 0; i < MAX_PLACARES; i++) {
        if (dados->copia.placar[i].identificador != 0) {
            total++;
        }
    }

    return total;
}

DWORD WINAPI threadNotificacoes(LPVOID lpParam) {
    HWND hwnd = (HWND)lpParam;
    DADOS_MONITOR* dados;
    HANDLE handles[2];
    DWORD r;

    dados = (DADOS_MONITOR*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    if (dados == NULL) {
        return 0;
    }

    while (WaitForSingleObject(dados->hEventoSair, 0) == WAIT_TIMEOUT) {

        if (!dados->centralDisponivel) {
            abrirMemoriaCentral(dados);
            PostMessage(hwnd, WM_ATUALIZAR_ALERTAS, 0, 0);
            Sleep(1000);
            continue;
        }

        handles[0] = dados->hEventoSair;
        handles[1] = dados->hEvento;

        r = WaitForMultipleObjects(
            2,
            handles,
            FALSE,
            1000
        );

        if (r == WAIT_TIMEOUT) {
            continue;
        }

        if (r == WAIT_OBJECT_0) {
            break;
        }

        if (r == WAIT_OBJECT_0 + 1) {
            ResetEvent(dados->hEvento);

            WaitForSingleObject(dados->hMutex, INFINITE);

            if (dados->shm != NULL) {
                CopyMemory(&dados->copia, dados->shm, sizeof(SHM_ALERTA));
            }

            ReleaseMutex(dados->hMutex);

            PostMessage(hwnd, WM_ATUALIZAR_ALERTAS, 0, 0);

            if (dados->copia.desligar) {
                break;
            }
        }
    }

    return 0;
}

void desenharMonitor(HWND hwnd, DADOS_MONITOR* dados) {
    PAINTSTRUCT ps;
    HDC hdc;
    TCHAR linha[300];
    int y = 20;
    int totalAtivos;
    int inicio;
    int fim;
    int indiceAtivo = 0;
    int totalPaginas;

    hdc = BeginPaint(hwnd, &ps);

    SetBkMode(hdc, TRANSPARENT);

    TextOut(
        hdc,
        20,
        y,
        TEXT("Monitor de Alertas Ativos"),
        (int)_tcslen(TEXT("Monitor de Alertas Ativos"))
    );

    y += 35;

    WaitForSingleObject(dados->hMutex, INFINITE);

    if (!dados->centralDisponivel) {
        TextOut(
            hdc,
            20,
            y,
            TEXT("Central nao disponivel. Confirme os nomes dos recursos na Configuracao."),
            (int)_tcslen(TEXT("Central nao disponivel. Confirme os nomes dos recursos na Configuracao."))
        );

        ReleaseMutex(dados->hMutex);
        EndPaint(hwnd, &ps);
        return;
    }

    if (dados->copia.desligar) {
        TextOut(
            hdc,
            20,
            y,
            TEXT("A plataforma foi encerrada pela central."),
            (int)_tcslen(TEXT("A plataforma foi encerrada pela central."))
        );

        ReleaseMutex(dados->hMutex);
        EndPaint(hwnd, &ps);
        return;
    }

    totalAtivos = contarAlertasAtivos(dados);

    if (dados->maxAlertasPagina <= 0) {
        dados->maxAlertasPagina = 5;
    }

    totalPaginas = (totalAtivos + dados->maxAlertasPagina - 1) / dados->maxAlertasPagina;

    if (totalPaginas <= 0) {
        totalPaginas = 1;
    }

    if (dados->paginaAtual >= totalPaginas) {
        dados->paginaAtual = totalPaginas - 1;
    }

    if (dados->paginaAtual < 0) {
        dados->paginaAtual = 0;
    }

    inicio = dados->paginaAtual * dados->maxAlertasPagina;
    fim = inicio + dados->maxAlertasPagina;

    _stprintf_s(
        linha,
        300,
        TEXT("Pagina %d/%d | Total de alertas ativos: %d"),
        dados->paginaAtual + 1,
        totalPaginas,
        totalAtivos
    );

    TextOut(hdc, 20, y, linha, (int)_tcslen(linha));

    y += 35;

    if (totalAtivos == 0) {
        TextOut(
            hdc,
            20,
            y,
            TEXT("Sem alertas ativos."),
            (int)_tcslen(TEXT("Sem alertas ativos."))
        );
    }
    else {
        for (int i = 0; i < MAX_PLACARES; i++) {
            if (dados->copia.placar[i].identificador != 0) {

                if (indiceAtivo >= inicio && indiceAtivo < fim) {
                    _stprintf_s(
                        linha,
                        300,
                        TEXT("Placar %lu | Duracao: %lu s | Mensagem: %s"),
                        dados->copia.placar[i].identificador,
                        dados->copia.placar[i].duracao,
                        dados->copia.placar[i].msg
                    );

                    TextOut(hdc, 20, y, linha, (int)_tcslen(linha));
                    y += 25;
                }

                indiceAtivo++;
            }
        }
    }

    y += 30;

    TextOut(
        hdc,
        20,
        y,
        TEXT("Use Page Down / Page Up para mudar de pagina."),
        (int)_tcslen(TEXT("Use Page Down / Page Up para mudar de pagina."))
    );

    ReleaseMutex(dados->hMutex);

    EndPaint(hwnd, &ps);
}

INT_PTR CALLBACK DlgConfig(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static DADOS_MONITOR* dados;

    switch (msg) {

    case WM_INITDIALOG:
        dados = (DADOS_MONITOR*)lParam;

        if (dados == NULL) {
            return FALSE;
        }

        SetDlgItemInt(dlg, IDC_MAX_ALERTAS, dados->maxAlertasPagina, FALSE);
        SetDlgItemText(dlg, IDC_MAX_ALERTAS2, dados->nomeMemoria);
        SetDlgItemText(dlg, IDC_MAX_ALERTAS3, dados->nomeEvento);

        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {

        case IDOK: {
            int novoMax;

            novoMax = GetDlgItemInt(dlg, IDC_MAX_ALERTAS, NULL, FALSE);

            if (novoMax <= 0 || novoMax > MAX_PLACARES) {
                MessageBox(
                    dlg,
                    TEXT("O numero maximo de alertas deve estar entre 1 e 20."),
                    TEXT("Configuracao invalida"),
                    MB_OK | MB_ICONWARNING
                );

                return TRUE;
            }

            dados->maxAlertasPagina = novoMax;

            GetDlgItemText(
                dlg,
                IDC_MAX_ALERTAS2,
                dados->nomeMemoria,
                TAM_NOME_RECURSO
            );

            GetDlgItemText(
                dlg,
                IDC_MAX_ALERTAS3,
                dados->nomeEvento,
                TAM_NOME_RECURSO
            );

            dados->paginaAtual = 0;

            EndDialog(dlg, IDOK);
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }

        break;

    case WM_CLOSE:
        EndDialog(dlg, IDCANCEL);
        return TRUE;
    }

    return FALSE;
}

LRESULT CALLBACK trataEventos(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DADOS_MONITOR* dados;

    dados = (DADOS_MONITOR*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {

    case WM_CREATE: {
        CREATESTRUCT* cs;

        cs = (CREATESTRUCT*)lParam;
        dados = (DADOS_MONITOR*)cs->lpCreateParams;

        SetWindowLongPtr(hwnd, GWLP_USERDATA,(LONG_PTR)dados);

        dados->hMutex= CreateMutex(NULL , FALSE , NULL);

        dados->hEventoSair = CreateEvent(NULL, TRUE, FALSE, NULL);

        dados->maxAlertasPagina = 5;
        dados->paginaAtual = 0;

        _tcscpy_s(dados->nomeMemoria, TAM_NOME_RECURSO, NOME_MEMORIA_ALERTAS);
        _tcscpy_s(dados->nomeEvento, TAM_NOME_RECURSO, NOME_EVENTO_ALERTAS);

        abrirMemoriaCentral(dados);

        dados->hThread = CreateThread(
            NULL,
            0,
            threadNotificacoes,
            hwnd,
            0,
            NULL
        );

        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {

        case ID_FICHEIRO_CONFIGURAR:
            DialogBoxParam(
                GetModuleHandle(NULL),
                MAKEINTRESOURCE(IDD_DIALOG1),
                hwnd,
                DlgConfig,
                (LPARAM)dados
            );

            abrirMemoriaCentral(dados);
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;

        case ID_FICHEIRO_ACERCA:
            MessageBox(
                hwnd,
                TEXT("SO2 - Monitor de Alertas\n\nAutor:\nNuno Rebelo - Numero 2022137005"),
                TEXT("Acerca"),
                MB_OK | MB_ICONINFORMATION
            );
            return 0;

        case ID_FICHEIRO_SAIR:
            DestroyWindow(hwnd);
            return 0;
        }

        return 0;

    case WM_KEYDOWN:
        if (dados != NULL) {
            int totalAtivos;
            int totalPaginas;

            WaitForSingleObject(dados->hMutex, INFINITE);

            totalAtivos = contarAlertasAtivos(dados);

            if (dados->maxAlertasPagina <= 0) {
                dados->maxAlertasPagina = 5;
            }

            totalPaginas = (totalAtivos + dados->maxAlertasPagina - 1) / dados->maxAlertasPagina;

            if (totalPaginas <= 0) {
                totalPaginas = 1;
            }

            switch (wParam) {

            case VK_NEXT:
                if (dados->paginaAtual < totalPaginas - 1) {
                    dados->paginaAtual++;
                }
                break;

            case VK_PRIOR:
                if (dados->paginaAtual > 0) {
                    dados->paginaAtual--;
                }
                break;
            }

            ReleaseMutex(dados->hMutex);

            InvalidateRect(hwnd, NULL, TRUE);
        }

        return 0;

    case WM_ATUALIZAR_ALERTAS:
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;

    case WM_PAINT:
        if (dados != NULL) {
            desenharMonitor(hwnd, dados);
        }
        return 0;

    case WM_DESTROY:
        if (dados != NULL) {
            if (dados->hEventoSair != NULL) {
                SetEvent(dados->hEventoSair);
            }

            if (dados->hThread != NULL) {
                WaitForSingleObject(dados->hThread, 1500);
                CloseHandle(dados->hThread);
                dados->hThread = NULL;
            }

            fecharMemoriaCentral(dados);

            if (dados->hEventoSair != NULL) {
                CloseHandle(dados->hEventoSair);
                dados->hEventoSair = NULL;
            }

            if (dados->hMutex != NULL)
            {
                CloseHandle(dados->hMutex);
                dados->hMutex = NULL;
            }
        }

        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI _tWinMain(
    HINSTANCE hInst,
    HINSTANCE hPrevInst,
    LPTSTR lpCmdLine,
    int nCmdShow
) {
    WNDCLASSEX wc;
    HWND hwnd;
    MSG msg;
    DADOS_MONITOR dados;

    ZeroMemory(&dados, sizeof(dados));

    wc.cbSize = sizeof(WNDCLASSEX);
    wc.hInstance = hInst;
    wc.lpszClassName = szProgName;
    wc.lpfnWndProc = trataEventos;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(NULL, IDI_INFORMATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszMenuName = MAKEINTRESOURCE(IDR_MENU1);
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);

    if (!RegisterClassEx(&wc)) {
        return 0;
    }

    hwnd = CreateWindow(
        szProgName,
        TEXT("SO2 - Monitor de Alertas"),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        800,
        500,
        HWND_DESKTOP,
        NULL,
        hInst,
        &dados
    );

    if (hwnd == NULL) {
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
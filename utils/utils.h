#include <windows.h>
#include <tchar.h>
#include <io.h>
#include <stdio.h>
#include <fcntl.h>


#define TAM 200
#define TAM_MSG 140

#define CHAVE_REGISTO TEXT("Software\\TrabSO2")
#define VALOR_NPIPE TEXT("NPIPE")

#define NOME_MEMORIA_ALERTAS TEXT("SO2_PLACAR_SHM_ALERTA")
#define NOME_EVENTO_ALERTAS  TEXT("SO2_PLACAR_EVT_ALERTA")


#define TIPO_LIGAR 1
#define TIPO_DESLIGAR 2
#define TIPO_FIM_ALERTA 3
#define TIPO_NOVO_ALERTA 4
#define TIPO_CANCELAR 5
#define TIPO_ENCERRAR 6
#define TIPO_ID 7

#define MAX_PLACARES 20
#define MAX_INSTANCIAS_PIPE (MAX_PLACARES + 1)
#define TAM_MSG 140
#define TAM_PIPE 200
#define TAM_LINHA 400

typedef struct {

    struct {
        DWORD identificador;
        DWORD duracao;
        TCHAR msg[TAM_MSG];
    }placar[20];

    BOOL desligar;
}SHM_ALERTA;


typedef struct {
    BYTE tipo;
} MSG_CMD;

typedef struct {
    BYTE tipo;
    TCHAR msg[TAM_MSG];
    DWORD duracao;
} MSG_ALERTA;

typedef struct {
    BYTE tipo;
    DWORD identificador;
} MSG_ID;

typedef struct {
    HANDLE hPipe;
    HANDLE hEventoSair;
    HANDLE hTimer;
    HANDLE hThreadRecebe;
    HANDLE hThreadTimer;

    CRITICAL_SECTION csEcra;
    CRITICAL_SECTION csPipe;
    CRITICAL_SECTION csEstado;

    TCHAR nomePipe[TAM];
    TCHAR caminhoPipe[TAM];

    DWORD identificador;
    BOOL ligado;
    BOOL timerAtivo;
} DADOS_PLACAR;

typedef struct {
    BOOL ocupado;
    BOOL remover;
    DWORD id;
    HANDLE hPipe;
    HANDLE hThread;
    BOOL alertaAtivo;
    TCHAR msgAtual[TAM_MSG];
    DWORD duracaoAtual;
    CRITICAL_SECTION csPipe;
}PLACAR;

typedef struct {
    TCHAR nomePipe[TAM_PIPE];
    TCHAR caminhoPipe[TAM_PIPE];
    PLACAR placares[MAX_PLACARES];
    DWORD proximoId;
    BOOL terminar;
    HANDLE hEventoSair;
    CRITICAL_SECTION csEstado;
    CRITICAL_SECTION csEcra;

    HANDLE hMapAlertas;
    HANDLE hEventoAlertas;
    SHM_ALERTA* shmAlertas;
} DADOS_CENTRAL;


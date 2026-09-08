Trabalho Prático - Sistemas Operativos 2
Ano letivo: 2025/2026

M3 - Programa Monitor e Memória Partilhada

Aluno:
Nuno Guilherme Sampaio Rebelo - 2022137005


Funcionalidades implementadas:

1. Programa Monitor
- Interface Gráfica Win32
- Janela principal para apresentação dos alertas ativos
- Atualização automática da informação apresentada

2. Menu "Ficheiro"
- Opção "Configuração"
- Opção "Acerca"
- Opção "Sair"

3. Configuração
- Implementação através de DialogBox
- Definição do número máximo de alertas apresentados
- Configuração dos nomes dos recursos de comunicação e sincronização

4. Acerca
- Implementação através de MessageBox
- Apresentação dos dados do autor:
	- Nuno Guilherme Sampaio Rebelo
	- 2022137005

5. Visualização de alertas
- Apresentação do identificador do placar
- Apresentação da mensagem do alerta
- Apresentação da duração associada ao alerta
- Atualização em tempo real dos alertas ativos

6. Paginação
- Suporte para múltiplas páginas de alertas
- Utilização das teclas Page Up e Page Down
- Navegação entre páginas quando o número de alertas excede o limite configurado

7. Memória Partilhada
- Utilização de memória partilhada para comunicação entre o Central e o Monitor
- Estrutura SHM_ALERTA conforme especificado no enunciado
- Partilha da lista de alertas ativos
- Partilha do estado de encerramento da plataforma

8. Alterações ao programa Central
- Criação da memória partilhada
- Atualização automática dos alertas ativos
- Atualização da informação dos placares
- Sinalização do encerramento da plataforma

9. Sincronização
- Utilização de mecanismos de sincronização para garantir a consistência dos dados
- Proteção de acesso concorrentes à memória partilhada
- Notificação imediata das alterações ao Monitor
- Sem utilização de polling

10. Encerramento da plataforma
- Libertação controlada de recursos
- Fecho da memória partilhada
- Encerramento da interface gráfica

Defesa Oral:
Não pretendo realizar a defesa oral individual presencial do Trabalho Prático.

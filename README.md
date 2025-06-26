# ConsoleApplication1
Configuração no Visual Studio

Crie um novo projeto "Console Application" em C++
Substitua o conteúdo do arquivo principal pelo código acima
Configure as propriedades do projeto:
Vá em "Project" > "Properties"
Em "Configuration Properties" > "Linker" > "Input"
Adicione "ws2_32.lib" em "Additional Dependencies"

Como executar

Compile o projeto (F7 ou Build > Build Solution)
Execute o programa (F5 ou Debug > Start Debugging)
Passe os parâmetros necessários:
Configure os argumentos em "Project" > "Properties" > "Debugging" > "Command Arguments"
Exemplo: 8080 example.com 80

Diferenças principais para Windows

Uso de Winsock2.h em vez de headers Unix
SOCKET em vez de int para descritores de socket
WSADATA e WSAStartup() para inicialização
Constantes diferentes para shutdown (SD_RECEIVE/SD_SEND)
Necessidade de vincular explicitamente a biblioteca ws2_32.lib

Este código mantém a mesma funcionalidade básica do proxy, mas agora é compatível com o ambiente do Visual Studio e Windows.

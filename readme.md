# RCT23D

Uma implementação de visualização 3D para o OpenRCT2, projeto de código aberto que reimplementa **RollerCoaster Tycoon 2**. Este repositório parte do código do OpenRCT2 e acrescenta um modo de câmera 3D ao jogo: o parque continua sendo simulado pelo motor original do OpenRCT2, enquanto um renderizador OpenGL reconstrói a cena a partir do terreno, dos elementos do mapa e dos sprites que o jogo já pinta.

> O projeto não inclui os arquivos proprietários de RollerCoaster Tycoon 2. Para jogar, é necessário possuir uma cópia legítima do jogo e indicar seus arquivos ao OpenRCT2.

## Estado do projeto

O modo 3D está em desenvolvimento nesta branch. A implementação adiciona câmera livre e em primeira pessoa, câmera a bordo de veículos, renderização de terreno e água, objetos derivados dos sprites existentes, sombras, controles de distância e campo de visão e alternância para a interface isométrica. A disponibilidade e o desempenho dependem do suporte a OpenGL do computador.

Atalho padrão para alternar a visualização: **F6**. A janela de configurações do modo 3D permite ajustar campo de visão, velocidade, sensibilidade, distância de desenho e opções visuais. Os atalhos podem ser alterados nas configurações do OpenRCT2.

## Requisitos

- Windows 10/11, Linux ou macOS compatível com os requisitos do OpenRCT2.
- CMake 3.24 ou superior e compilador C++20 compatível. No Windows, use Visual Studio 2022 com a carga de trabalho **Desenvolvimento para desktop com C++**.
- Dependências de compilação do OpenRCT2, incluindo SDL2 e OpenGL. Consulte as instruções oficiais da sua plataforma para instalar as dependências e configurar o vcpkg quando necessário.
- Uma instalação legítima de RollerCoaster Tycoon 2 para obter os dados do jogo. Os dados não são distribuídos por este repositório.

## Instalação para jogar

1. Obtenha uma compilação deste projeto ou compile-o seguindo a seção abaixo.
2. Instale RollerCoaster Tycoon 2 por uma loja legítima, como Steam ou GOG.
3. Abra o OpenRCT2 e, na primeira execução, informe a pasta que contém os arquivos do RCT2 quando solicitado. Se necessário, configure o caminho nas opções de diretórios do OpenRCT2.
4. Carregue um parque e pressione **F6** para alternar entre a câmera isométrica e a visualização 3D.
5. Abra as configurações da visualização 3D para ajustar a câmera e as opções gráficas.

Para instruções detalhadas de instalação de dependências e configuração específica por sistema operacional, consulte [Building OpenRCT2](https://github.com/OpenRCT2/OpenRCT2/wiki/Building-OpenRCT2).

## Compilar a partir do código-fonte

Clone o repositório e inicialize os submódulos:

```bash
git clone https://github.com/Fabiusmaia/RCT23D.git
cd RCT23D
git submodule update --init --recursive
```

Instale e configure as dependências conforme o guia oficial da plataforma. Em seguida, gere a compilação com CMake. Exemplo para Windows com Visual Studio 2022:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Exemplo para Linux ou macOS usando o gerador padrão:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Os comandos de configuração pressupõem que as dependências exigidas pelo OpenRCT2 já estão disponíveis no ambiente. Algumas plataformas ou configurações usam um arquivo de toolchain do vcpkg; siga o guia oficial para obter os parâmetros corretos para sua instalação. A saída e a etapa de instalação variam conforme o sistema e o gerador CMake utilizado.

## Arquitetura

O modo 3D foi integrado ao fluxo existente do OpenRCT2 para reutilizar a simulação, os elementos do mapa, a paleta e o conteúdo gráfico do jogo.

```text
Simulação e mapa do OpenRCT2
        │
        ├── PaintCapture: captura os sprites emitidos pelo pintor isométrico
        ├── SceneBuilder: constrói terreno, água, trilhos, objetos e entidades
        ├── Camera3D / Render3D: câmera, modos de movimento e estado do recurso
        └── Renderer3D (OpenGL): texturas, malhas, shaders, sombras e composição
                    │
                    └── framebuffer 3D composto sob a interface normal do jogo
```

### Componentes principais

- `src/openrct2/render3d/`: lógica independente de renderização OpenGL. Inclui câmera e matemática 3D, captura da saída do pintor, tipos de cena e construção de terreno, sprites, trilhos e entidades. `WalkCollision` calcula colisões e superfícies para o modo em primeira pessoa.
- `src/openrct2-ui/drawing/engines/opengl/Renderer3D.*`: backend gráfico. Constrói e mantém malhas por blocos do mapa, prepara atlas de texturas a partir dos sprites, aplica shaders e sombras e compõe o resultado em um framebuffer.
- `data/shaders/render3d*`: shaders de cena e céu usados pelo renderer.
- `src/openrct2-ui/windows/View3DSettings.cpp`: janela de opções da câmera e do modo 3D.
- `src/openrct2-ui/input/` e `src/openrct2-ui/WindowManager.cpp`: integração de atalhos, mouse e alternância de interação entre o viewport 2D e a câmera 3D.
- `src/openrct2/config/Config.*`: persistência das opções da câmera e da visualização.
- `test/tests/Render3DTests.cpp`: testes unitários para as partes de geometria e construção da cena que podem ser validadas sem desenhar a interface.

O cenário é particionado em blocos para construir e atualizar a geometria conforme o mapa muda. Os sprites são capturados nas quatro rotações isométricas do jogo e usados como base para reconstruir objetos na cena 3D. Assim, grande parte do conteúdo original pode ser exibida sem substituir os assets do RCT2.

## Controles

- **F6**: alterna entre a vista isométrica e a vista 3D.
- **Mouse**: olha ao redor quando a câmera 3D controla o viewport principal.
- **Roda do mouse**: ajusta o movimento/zoom conforme o modo da câmera.
- **WASD e controles de movimento configurados no jogo**: navegação da câmera; os vínculos usam as ações de movimento do OpenRCT2.
- **Configurações da vista 3D**: alterna câmera livre/primeira pessoa, inverte o eixo vertical, suaviza sprites, ativa sombras e permite acompanhar o veículo mais próximo.

Os vínculos exatos de teclado podem variar com o idioma, a configuração de atalhos e o modo da câmera. Consulte os atalhos dentro do jogo.

## Limitações conhecidas

- A reconstrução 3D aproveita sprites e dados do pintor isométrico, então a aparência de alguns objetos pode não corresponder a modelos 3D nativos.
- A vista depende do renderer OpenGL e pode ser desativada automaticamente se a inicialização gráfica falhar.
- O custo de renderização varia com o tamanho do parque, a distância de desenho, a placa de vídeo e a disponibilidade de sombras por hardware.
- O modo 3D é uma contribuição em desenvolvimento sobre o OpenRCT2; problemas específicos devem ser relatados com o sistema operacional, hardware gráfico e passos para reproduzir.

## Como contribuir

Abra uma issue descrevendo o problema, o sistema operacional e, quando possível, os passos para reproduzi-lo. Para propor mudanças, crie uma branch e envie um pull request. Consulte [CONTRIBUTING.md](CONTRIBUTING.md) e as instruções de compilação do OpenRCT2 antes de começar.

## Licença e créditos

O código segue a licença GNU GPL v3 ou posterior, conforme [licence.txt](licence.txt). O OpenRCT2 é uma reimplementação independente de RollerCoaster Tycoon 2 e seus créditos e colaboradores estão listados em [contributors.md](contributors.md). RollerCoaster Tycoon 2 é propriedade de seus respectivos titulares; este repositório não distribui o jogo nem seus dados proprietários.

## Links

- [Repositório RCT23D](https://github.com/Fabiusmaia/RCT23D)
- [Projeto OpenRCT2 original](https://github.com/OpenRCT2/OpenRCT2)
- [Guia oficial de compilação](https://github.com/OpenRCT2/OpenRCT2/wiki/Building-OpenRCT2)
- [OpenRCT2.io](https://openrct2.io/)

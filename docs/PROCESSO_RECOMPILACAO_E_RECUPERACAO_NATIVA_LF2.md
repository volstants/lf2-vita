# Processo usado no Little Fighter 2: recompilação estática e recuperação nativa

Referência metodológica para outros projetos. Consolidado em 06/10/2026 a partir dos fontes, planos, relatórios e histórico do LF2. Este documento descreve o processo; não substitui o plano ativo nem autoriza uma nova implementação.

Nesta consolidação foram consultados arquivos e registros e foi conferido o SHA-256 do executável original. Não foram repetidos builds, testes de gameplay, benchmarks ou testes no Vita. Os resultados abaixo são resultados anteriores, identificados pela rodada.

## 1. O que fizemos e até onde chegamos

Partimos do Little Fighter 2 v2.0a para Windows, sem o código-fonte original. Construímos uma recompilação estática do executável x86: um tradutor produz C durante o build, e o VitaSDK compila esse C para ARM. Adaptamos as APIs do Windows/DirectX ao host do Vita. Essa linha produziu a série 2.x, com testes de jogo no console.

Depois começamos a recuperação de funções em C legível, substituindo progressivamente suas versões traduzidas numa bancada de PC. Essa segunda linha é o trabalho de descompilação nativa, ainda em D2 nas entregas concluídas consultadas. A independência do PE original, a conversão completa para memória portátil e a entrega final desse motor nativo no Vita continuam como marcos futuros.

```text
                                  NO PC, DURANTE O BUILD
lf2.exe x86 ── análise/CFG ── tradução para C ── GCC/VitaSDK ── código ARM
                                                                  │
                                  NO VITA                         ▼
                                 runtime + APIs compatíveis + host SDL

                         RECUPERAÇÃO PROGRESSIVA, NO PC
bytes/assembly + Ghidra ── C recuperado ── hook no lugar da função traduzida
                                             │
                                 suíte + diferencial + pilha + cobertura
                                             │
                         remover dependências do original → PC portátil → Vita
```

O Vita executa ARM previamente compilado. Na linha estática, esse ARM ainda representa registradores, flags, pilha x87 e memória do x86 por meio do runtime. Não há decodificação de x86 por interpretador ou JIT durante o jogo. A tradução estática não implica, por si só, C idiomático ou independência de toda a representação da máquina original.

Unicorn foi usado no PC como ferramenta de referência. Não é o motor entregue no Vita.

## 2. Ponto de partida: identificar e preservar a referência

Fixamos uma versão específica do jogo e seus dados. O executável usado tem SHA-256:

```text
12dfa00f6b767508612550e9ab27ab74b4201ff4cb9ff31d068925924eab8fc5
```

O hash foi conferido novamente nesta consolidação. O analisador consultado contém uma verificação desse alvo; trocar somente o caminho do EXE não transforma o tradutor num recompilador para qualquer jogo.

Inventariamos o formato PE32, arquitetura x86, seções, endereço de entrada, recursos, importações, DLLs e indícios do compilador. Os registros identificam MSVC 8.0/Visual C++ 2005 e 163 importações de 13 DLLs, incluindo DirectDraw, DirectSound, WinMM, rede, WinINet, DirectShow e o runtime C/C++.

Preservamos executável, dados, exportações antigas e backups por rodada em `reference/`. As evidências são vinculadas a uma versão/hash. Os trabalhos pesados do Astra usaram cópias isoladas no ext4 do WSL, conferidas por manifestos contra a base Windows; a pasta canônica do jogo permaneceu no OneDrive.

**Parâmetro para outros projetos:** antes de escolher a ferramenta, produzir uma ficha com versão, hash, arquitetura, compilador provável, APIs, dados necessários e escopo funcional. Identificar separadamente o que é jogo, biblioteca e adaptação de plataforma.

## 3. Por que mudamos da reimplementação para a recompilação

A linha antiga, preservada como v1.09, reimplementava o motor à mão a partir de comportamento, formatos e análise. As auditorias encontraram diferenças e hipóteses incorretas, inclusive sobre constantes, aleatoriedade e mecanismos de estados.

A lição foi usar evidência executável para decidir. Código de outros projetos e pseudocódigo ajudam a formular hipóteses; os bytes/assembly do alvo e as comparações de execução sustentam a conclusão.

A recompilação estática passou a ser a base funcional porque preserva mecanicamente muito mais da lógica original e oferece uma referência executável durante a recuperação posterior. A reimplementação foi preservada como histórico, sem substituir a referência original.

Isso não torna a recompilação infalível: a fidelidade também depende do analisador, do tradutor, do runtime, das APIs compatíveis e das escolhas de precisão numérica.

## 4. Análise e recuperação do fluxo de execução

`tools/recomp/analyze.py` usa `pefile` para ler o PE e Capstone para decodificar instruções x86. Parte de entradas conhecidas, incluindo o entry point e sementes do jogo, e percorre o fluxo alcançável.

O trabalho inclui identificar chamadas, blocos, saltos condicionais, tabelas de salto e pontos indiretos. Callbacks e endereços presentes em tabelas exigem atenção: uma busca somente por chamadas diretas não encontra todas as entradas. Saltos indiretos sem resolução precisam ser tratados antes de considerar a geração utilizável.

Ghidra foi usado em paralelo para investigar funções, chamadas, dados, tipos e fragmentos de exceção. Na auditoria de 20/09, trabalhamos numa cópia do banco e exportamos JSON por função com pseudocódigo, bytes, referências, P-code e CFG, além do C consolidado e assembly.

A auditoria passou de 372 entradas no banco/exportação principal para 403 funções decompiladas com sucesso e confrontou 73.587 instruções reconhecidas com os bytes e comprimentos decodificados. Fragmentos SEH foram tratados separadamente, pois dependem do contexto da função pai.

**403 não significa 403 funções de gameplay nem 100% do fonte recuperado.** A contagem inclui suporte e thunks. O inventário posterior usado para acompanhar a recuperação separa 182 funções do jogo. Contagens de catálogos distintos exigem denominadores explícitos.

Também evitamos promover automaticamente padding, alinhamento e trechos órfãos a funções. Um JSON que contém uma mensagem de falha não conta como decompilação concluída.

**Parâmetro:** conservar bytes e endereço em cada conclusão, documentar fronteiras incertas e conferir completude da exportação. Quantidade de pseudocódigo não mede fidelidade.

## 5. Tradução estática x86 → C → ARM

`tools/recomp/emit.py` transforma as funções recuperadas pelo analisador em funções C, como `f_004xxxxx`, distribuídas em `lf2_gen_*.c`, com despacho em `lf2_dispatch.c`.

O C traduzido conserva a semântica do x86 por meio de:

- registradores representados no estado `R`;
- memória guest de 32 bits e endereçamento compatível;
- flags condicionais calculadas sob demanda;
- pilha e argumentos segundo o comportamento original;
- pilha x87 representada em software;
- despacho de chamadas indiretas e importações para o runtime/HLE.

O GCC compila essa representação para PC nas verificações e o GCC do VitaSDK a compila para ARM no port. Os arquivos gerados ficam nos intermediários de build.

Na série estática, o EXE também passou a ser embutido no programa por `lf2_image.S`. Ele fornece a imagem/dados necessários ao runtime; isso não significa que o Vita execute o PE como um Windows. O código de execução já foi traduzido e compilado.

Substituições nativas pontuais, como `ftol` e o decifrador de `.dat`, foram introduzidas no baseline estático. Isso também não equivale à recuperação completa do motor.

**Parâmetro:** separar núcleo do tradutor, configuração do alvo e runtime. No LF2, uma entrega de 29/09 externalizou hash, sementes e mapas e preservou 13 arquivos C/cabeçalho byte a byte. Essa entrega foi feita em cópia WSL e patch; não se deve presumir que todos esses arquivos já estão integrados ao checkout canônico.

## 6. Runtime e adaptação das APIs

O tradutor resolve as instruções da CPU, mas o jogo ainda chama serviços do sistema. Implementamos a superfície necessária das APIs em:

| Componente | Responsabilidade |
|---|---|
| `src/recomp/recomp_rt.h` e `rt_core.cpp` | Estado guest, memória, pilha, flags, x87 e despacho |
| `src/recomp/win32_api.cpp` | Arquivos, tempo, mensagens, alocações e funções de suporte |
| `src/recomp/win32_dx.cpp` | Superfícies, desenho e serviços DirectX usados pelo jogo |
| `src/recomp/host_sdl.cpp` | Vídeo, entrada, áudio e execução no PC/Vita |
| `src/recomp/host_test.cpp` | Execução determinística e observação para testes no PC |

A implementação trata o comportamento que o jogo observa, sem exigir a reprodução de um Windows inteiro. Nos testes, layout de memória, alocação, tempo virtual e aleatoriedade são controlados para permitir comparação.

A entrega do console recebeu decisões próprias de interface: controles do Vita, atalhos, navegação por D-pad, apresentação em 960×544, opção de HUD e adaptações de áudio/arquivos. Essas escolhas devem ser distinguidas de mudanças na lógica de combate.

**Parâmetro:** levantar APIs e efeitos observáveis antes de começar o port. Cada nova API de outro jogo pode exigir implementação, teste e uma correspondente atualização da referência no PC.

## 7. Oráculo e determinismo

`tools/emu/` executa o EXE original no Unicorn, no PC, com APIs implementadas em Python em `runtime.py`. Essa referência executa as instruções originais; o outro lado executa a tradução compilada com a camada C++.

Controlamos entradas por eventos vinculados a frames, tempo virtual, estado aleatório, memória e alocação. `runtime.py` e a camada C++ precisam manter o mesmo contrato. A ordem e a quantidade de chamadas ao RNG importam tanto quanto seus valores iniciais.

Nas investigações anteriores do processo Windows real, também identificamos que a tabela aleatória muda no boot. Para traços desse processo, era necessário capturar/injetar tabela e cursores. Na bancada posterior, o ambiente determinístico controla a inicialização: são mecanismos distintos para resolver a mesma necessidade de reprodução.

Cada cenário conserva `events.txt`, `marks.json` e `oracle_hash.log`. A comparação observa CRC da `.data` por frame, heap a cada dez frames e, nas ferramentas apropriadas, memória final e traço de objetos.

Um oráculo executando com HLE testa o comportamento nesse ambiente de referência. Ele não comprova sozinho todos os efeitos de um Windows/DirectX real. Por isso há leitura de assembly, testes dirigidos e verificação no console.

**Parâmetro:** demonstrar primeiro que a referência é reproduzível. Só depois atribuir diferenças ao código recuperado.

## 8. Suíte de aceitação e ampliação da cobertura

A suíte original tem nove cenários, 27.383 frames ao todo, cobrindo personagens, especiais, colisões, estados, transformações, arremessos, muitos objetos e Stage.

O executor exige todos os arquivos obrigatórios, conclusão de todos os frames, hashes iguais e saída bem-sucedida. Logs vazios ou truncados e execuções abortadas reprovam. Não basta procurar uma linha positiva no log.

A recuperação nativa ganhou verificações adicionais:

1. Suíte original contra os hashes do EXE.
2. Telas com hooks versus tradução sem hooks.
3. Cenários adicionais de Battle, Championship, Survival, gravação e fusão.
4. Roteiros específicos, como digitação e o ensaio de replay de uma rodada.
5. Diferenciais por função e contexto.
6. Cobertura e auditoria da pilha guest.
7. Build normal sem hooks para detectar regressões na infraestrutura comum.

Os cinco adicionais passaram em rodadas consultadas, mas permaneciam fora do `suite.txt` original. Um teste adicional concluído não se torna automaticamente um gate permanente.

As telas dos relatórios nativos comparam hooks e referência traduzida. Não são capturas de pixels extraídas diretamente do EXE rodando no Windows.

**Lição concreta:** o conjunto antigo passava, mas um roteiro novo encontrou erro de digitação após End no frame 145. A correção foi reproduzida contra a árvore anterior, isolada desligando 4246b0 e validada com três oráculos regenerados. Cobertura é requisito contínuo, não um selo obtido uma única vez.

## 9. Medir e otimizar a linha estática

Separamos custo da lógica traduzida, custo da compatibilidade e custo do host. Usamos contagem de instruções, logs do Vita e preparação de perfil por função. O perfil foi validado para preservar comportamento; coletar o perfil completo no aparelho continuava sendo uma etapa própria.

| Achado registrado | Ação |
|---|---|
| Decifrador com operações de CRT por byte | Substituição nativa compatível |
| Muitas apresentações durante loading | Limitar apresentação no mesmo quadro |
| Conversão de som no primeiro uso | Conversor mais rápido/cache; depois conversão em segundo plano |
| Leitura custosa pela Newlib no Vita | Caminho com `sceIoRead` na v2.06 |
| Tempo de frame incluía apresentação/espera | Log separando esses custos |

Resultados anteriores: a carga da primeira versão estática era aproximadamente 86 s no Vita; a v2.05 registrou 36,1 s e luta a 30 quadros/s. São medições de versões/cenários históricos, não promessa de desempenho para outro jogo. O ganho da v2.06 ainda precisava de medição específica nos registros consultados.

**Parâmetro:** otimizar um gargalo medido, registrar antes/depois e repetir os gates. Um ganho do host não prova ganho do tradutor; reduzir instruções no PC não fornece automaticamente o tempo final no console.

## 10. Fundação da recuperação nativa: harness e hooks

Construímos `private/harness/`, que gera `bin/lf2-h32`. É a tradução do LF2 compilada para Linux x86 de 32 bits, com memória guest mapeada nos endereços originais e GCC/SSE na configuração padrão.

Cada função C recuperada é registrada em `private/src/registry.c`. `H(endereço)` instala o hook; `HS(endereço, spec)` também fornece uma especificação de teste diferencial. As funções restantes continuam traduzidas.

O hook substitui a função traduzida dentro da bancada. Não foi implementado o desenho antigo de injetar todo o C recuperado diretamente no EXE dentro do Unicorn. A comparação com as instruções originais chega pelos hashes do oráculo e demais instrumentos.

Esse arranjo permite recuperar uma unidade de cada vez e desligar um hook para isolar uma regressão, sem exigir que todo o jogo esteja recuperado.

**Parâmetro:** manter uma referência executável ao lado da implementação candidata, com comutação por função e testes do programa completo.

## 11. Ciclo usado por função e por lote

1. Escolher uma unidade por dependência, semelhança e relevância; começar por folhas e pilotos pequenos.
2. Consultar bytes, assembly, pseudocódigo, chamadores e dados.
3. Definir structs e globais com offsets comprovados.
4. Documentar contrato de chamada: argumentos, `this`, retorno, limpeza, registradores e profundidade da pilha nos pontos de chamada.
5. Escrever C recuperado e instalar o hook.
6. Executar o roteiro que alcança a função e o diferencial correspondente.
7. Executar o conjunto de gates globais e comparar a pilha com a linha de base.
8. Pedir revisão independente, preservando comandos, logs, manifestos e limitações.
9. Classificar a evidência e decidir integração ou adiamento.

Mudanças de structs/globais e novas funções são separadas em lotes para facilitar atribuição das diferenças. Funções grandes são quebradas em blocos documentados. O plano prevê até dez tentativas por função; se não fechar, registrar a melhor situação e o motivo sem declarar conclusão.

O percentual é acompanhado por bytes de funções do jogo, além da quantidade de funções. Completar muitas folhas pequenas não equivale a completar o motor.

Um hook instalado e uma suíte verde não verificam uma função que nenhum teste alcançou. Os estados distinguem traduzida, funcional, testada diferencialmente, equivalência formal sob hipóteses, match normalizado e exata. Quantidade de trials não substitui análise da cobertura e das entradas.

## 12. Diferencial, ABI, pilha e precisão numérica

O diferencial salva o estado, executa a função traduzida e a recuperada com as mesmas entradas, restaura entre os lados e compara retorno observado, `.data`, heap e scratch. Tempo e RNG também precisam ser restaurados.

Ele não compara automaticamente todo registrador, toda a pilha, arquivo ou framebuffer. O caminho de chamada pode repor ESP e esconder um erro de limpeza. Por isso acrescentamos `esp_audit.py`, que instrumenta entradas da tradução e compara sequências/profundidades com e sem hooks.

**Exemplo de pilha:** a chamada direta em C de 419a60 para 406ba0 omitia 0x24 bytes do contexto guest: 0x10 da chamada e 0x14 do frame. Consertar somente o wrapper externo não alcançaria o caminho interno. Isso exigiu análise por callsite e um observador de profundidade/interface.

**Exemplo numérico:** a variante com `-mfpmath=387` passou em apenas 7/9 cenários. A divergência também aparecia sem hooks, foi isolada na tradução de 406ba0 e estava ligada à palavra de controle x87 host `037f`, diferente da guest `027f`. A bancada com host controlado em `027f` passou 9/9. A variante sem controle e a controlada permanecem resultados separados.

As flags do C recuperado incluem `-fwrapv`, `-fno-strict-aliasing` e `-ffp-contract=off`. O projeto proíbe `-ffast-math` nessa recuperação. A semântica de conversões, divisão, shifts e consumo de RNG deve acompanhar a referência.

**Parâmetro:** não alterar flags, precisão ou hashes para fazer o teste passar. Isolar a causa e acrescentar a observação que faltava. Igualdade observada em SSE nos dois lados não verifica por si só a semântica x87 original.

## 13. A bancada também precisou de testes

Uma revisão descobriu que `TRIALS=0` e specs inválidas podiam resultar em aprovação sem comparação válida. Reforçamos a infraestrutura antes de usá-la como gate de outro lote.

O lote concluído em 04/10 valida trials, seleção de alvos, duplicações, gramática, limites e substituição de specs. Configurações rejeitadas saem com código 5 e sem a mensagem de aprovação.

O auditor de ESP ganhou linha de base e comparação integral do JSON relatado. Igualdade com essa base é aceita; remover desvios conhecidos gera um resultado separado, que exige revisão da nova base. Isso evita transformar uma alteração da cobertura em aprovação automática.

Resultados registrados do lote: 46 rejeições, seis controles positivos e nove controles de ESP, totalizando 61 controles; preservação de quatro sequências do gerador de 300 trials; suíte/telas 9/9, adicionais 5/5, digitação 3/3 e 24.300 trials diferenciais.

**Igual à linha de base não significa pilha perfeita.** A base usada nesse lote continha sete endereços com desvios conhecidos. A aceitação atesta ausência de mudança inesperada nos campos relatados, não eliminação de todos os problemas.

## 14. Estado documentado e marcos restantes

A fotografia quantitativa de 02/10 registra 177 hooks em 182 funções de jogo: 174 recuperadas, cobrindo 144.930 de 253.003 bytes (57,3%); três hooks parciais ficam fora desse percentual. Cinco funções estavam sem hook. Havia 35 funções recuperadas sem evidência por roteiro ou diferencial no cruzamento ampliado daquela data.

Em 03/10 foi concluída a validação da digitação. Em 04/10 foi integrado o endurecimento da bancada, ainda em D2. O histórico mais recente consultado relata avanço do ensaio de 419a60 e execução dos seus gates globais, mas está interrompido e não contém uma entrega final equivalente ao relatório do lote 1. Esta consolidação não promove esse ensaio a lote concluído nem presume que 406ba0 já foi recuperada.

| Marco | Critério | Situação nos registros consultados |
|---|---|---|
| N0 | Tradução estática executável no Vita | Baseline existente; desempenho por versão |
| D0 | Harness com substituição por hooks e comparação | Construído |
| D1 | Piloto de funções pequenas | Realizado |
| D2 | Recuperação progressiva com evidência por função/lote | Em andamento |
| D3 | Jogo sem o código original carregado; globais/tabelas próprias | Não demonstrado |
| D4 | Motor portátil no PC, APIs sobre SDL e ponteiros próprios | Não demonstrado |
| D5 | Motor recuperado compilado e validado no Vita | Não demonstrado |

O harness ainda carrega o PE, mantém globais em endereços fixos e depende de CRT/zlib e partes traduzidas. A ferramenta `d3_preflight.py` inventaria essas dependências; declara explicitamente que não certifica D3.

O método 4, matching do código de máquina original com ambiente MSVC compatível, é uma meta separada. Não há medição concluída desse matching nos relatórios consultados. Equivalência funcional permite avançar pelos marcos do port; não substitui correspondência binária exata.

Rede e replay passaram por delimitações de escopo ao longo das rodadas. Um replay de referência validado numa rodada não significa recuperação nativa de replay concluída. O escopo de outro jogo precisa ser fixado expressamente.

## 15. Build, empacotamento e teste no Vita

O build estático usa CMake/VitaSDK, SDL2 e SDL2_mixer. `build-vita.sh` produz dois VPKs: programa, para quem já tem os dados, e completo, com os dados. Na série com EXE embutido, ambos contêm esse material dentro do programa.

Distinguimos as identidades da série estática 2.x e da linha de desenvolvimento nativa 0.x. O primeiro pacote denominado nativo/perfil ainda executava N0: o nome da linha não certifica D5.

Um crash da 0.01 ocorreu antes de `main`. Foram preservados dump, ELF/SELF/VELF e hashes para cruzar o executável instalado com o pacote. A análise encontrou falha de reserva de memória e `ATTRIBUTE2=0` no SFO, enquanto a configuração exigida pelo build usava `12`. A entrega 0.02 corrigiu o empacotamento e acrescentou verificações de VPK/SFO; seu teste no aparelho ainda era pendente no relatório.

**Parâmetro:** validar separadamente código, pacote, instalação e execução. Build com saída zero e ZIP íntegro não demonstram abertura da aplicação ou gameplay. Para cada crash, conservar o binário exato e identificar a versão antes de simbolizar o dump.

## 16. Organização, autoria e entrega

Rodrigo conduziu as decisões, os testes físicos no Vita e o Git. Claude/Opus implementou o tradutor, o runtime, as APIs, o host e grande parte da recuperação. Astra/Codex trabalhou na infraestrutura, validação independente, contratos e ferramentas, além de unidades de jogo expressamente delegadas. A divisão é histórica, não uma ordem para iniciar agentes.

O modelo usado foi: unidade delimitada → implementação → revisão por outro participante → evidência executável → decisão. Primeira versão com IA; reescritas manuais posteriores por Rodrigo com orientação/revisão, preservando o baseline.

Cada entrega deve conter objetivo, arquivos, identidade do EXE, alterações, comandos, ambiente, códigos de saída, cobertura, logs, hashes, limites, autor/revisor e próximo marco. Não reapresentar um resultado antigo como execução nova.

Executáveis, dados, dumps e C gerado/recuperado permanecem privados conforme as decisões do projeto. Git e publicação ficam com Rodrigo. Isso registra a política adotada aqui; não é uma conclusão jurídica universal sobre outros projetos.

## 17. O que pode ser reaproveitado

| Parte | Reuso possível | O que precisa ser refeito/adaptado |
|---|---|---|
| Processo e critérios | Identidade, oráculo, hooks, gates, revisão e evidências | Escopo e cobertura do novo jogo |
| Analisador/tradutor | Infraestrutura para PE/x86 e instruções suportadas | Hash, sementes, fluxos indiretos e instruções novas |
| Runtime | Parte da representação x86 e despacho | Layout e requisitos não presentes no LF2 |
| HLE | APIs já implementadas | Novas APIs, efeitos, formatos e gráficos |
| Host Vita/SDL | Base de vídeo, entrada, áudio e arquivos | Interface, desempenho e memória do alvo |
| Suíte/diferencial | Formato e infraestrutura | Cenários, hashes, assinaturas e geradores coerentes |
| C recuperado e structs LF2 | Referência de método | Lógica do novo jogo é trabalho próprio |

O tradutor continua acoplado ao LF2. `vita-recomp-kit` é um destino de extração planejado, não um kit genérico funcional já demonstrado. A extração prevista exige C gerado idêntico, suíte preservada e builds conferidos.

## 18. Modelo para aplicar em outro projeto

Usar estas etapas como critérios de avanço, adaptando a rota ao material disponível:

| Etapa | Entrega mínima | Critério de avanço |
|---|---|---|
| Identificação | Binário/dados, versão, hashes, arquitetura e imports | Alvo reproduzível e escopo definido |
| Referência | Runner/traço determinístico e cenário inicial | A referência reproduz seus próprios resultados |
| Unidade piloto | Tradução ou recuperação compilável delimitada | Comparação observável com o original |
| Baseline funcional | Fluxo necessário e APIs suficientes | Cenários completos passam |
| Plataforma | Host/build/pacote do destino | Abre e executa o cenário no aparelho |
| Otimização | Perfil e alteração localizada | Ganho medido sem regressão |
| Recuperação progressiva | Funções/lotes com contratos e cobertura | Gates por lote e revisão independente |
| Independência | Nenhum fallback proibido e dados/globais próprios | Execução sem carregar código original |
| Portabilidade | Layouts/API/ponteiros adaptados | Comportamento preservado no PC portátil |
| Entrega nativa | Build final e teste do hardware | Gameplay, memória e desempenho aceitos |
| Matching, se requerido | Ambiente original e comparação de bytes | Percentual medido, com normalizações explícitas |

Para uma recompilação estática, não exigir descompilação completa antes de provar a tradução. Para um projeto com fonte suficiente, reproduzir e validar esse fonte pode evitar recuperação desnecessária. A escolha depende do alvo; o LF2 fornece o processo de verificação, não uma garantia de viabilidade.

### Ficha de uma unidade de trabalho

```text
Projeto / versão / hash:
Rota e objetivo final:
Unidade autorizada:
Dependências e contratos:
Referência preservada:
Arquivos da mudança:
Ambiente e flags:
Cenários / entradas / cobertura:
Comparações e critérios de aprovação:
Resultado esperado / obtido / códigos de saída:
Limites e caminhos não testados:
Autor / revisor:
Integração e próximo marco:
```

### Comandos de referência do LF2

Exemplos para Linux/WSL na raiz da cópia de trabalho, com dependências e dados preparados. Não foram executados nesta consolidação. Outros jogos precisam de configuração própria.

```bash
# Baseline estático no PC e suíte obrigatória.
make -f Makefile.recomp bin/lf2-recomp-test
bash tools/scen/check_suite.sh reference/lf2-game 2

# Recuperação no harness de 32 bits e a mesma suíte com hooks.
make -C private/harness
LF2_BIN=bin/lf2-h32 bash tools/scen/check_suite.sh reference/lf2-game 2

# Geração dos VPKs, com VitaSDK, Python e insumos configurados.
./build-vita.sh
```

Esses comandos não executam sozinhos todos os gates adicionais, diferenciais, observadores ou testes físicos. Os runners e comandos de cada entrega preservam os parâmetros exatos dessas verificações.

## 19. Fontes locais desta consolidação

- [Entrada do projeto](../LEIA-ME.md), plano específico e [estado quantitativo de 02/10](../private/STATUS.md).
- Método estático registrado anteriormente e [histórico v2.00–v2.06](sessoes/RESUMO_2026-09-23_a_25.md).
- Auditoria Ghidra e [procedimento de extração](../tools/DECOMPILE.md).
- [Formato e critérios da suíte](../tests/scen/LEIA-ME.md).
- Ferramentas/perfil de 29/09 e crash/empacotamento.
- Validação ampliada D2 de 02/10.
- Validação da digitação de 03/10.
- Endurecimento concluído de 04/10.
- Fontes inspecionados: `analyze.py`, `emit.py`, `guest_layout.h`, runtime/HLE, Makefiles, `esp_audit.py`, `test_diff_inputs.py` e `d3_preflight.py`.
- Plano geral e catálogo: `PSVita/Projects2Vita/MASTER PLAN.md` e `catalogo/PROJETOS_LOCAIS.md`.
- Histórico consultado: chat “Validar descompilação nativa do LF2”, incluindo as entregas de 02–04/10 e o ensaio mais recente ainda sem encerramento.

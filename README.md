# TopLifeMiami-Nivel-de-gua
Projeto para monitoramento de nível de água do Miami Beach



## Conceito

Utilizar um Sensor ultrassonico para medir o que falta de água no tanque, e calcular o que tem de água.
Então, exibir a medida atual na tela e a cada período de tempo, publicar os dados na internet.

![](imagens/sensor-de-nível-ultrassonico.jpg)

Foram explorados três métodos de publicação na Internet:

- Plataforma **Adafruit.io**, um broquer MQTT popular em projetos de automação
- Um site particular, representando um sistema hospedado na nuvem
- API do telegram, enviando mensagens periódicas, representando um sistema de alerta



## Material RECOMENDADO

> Nota: Alguns materiais, como cano e cabo, podem ser adquiridos por metro, diminuindo o custo. Ou mesmo utilizar resto que já exista em estoque.

### Itens Essenciais

#### Sensor de Ultrassom
![](imagens/Screenshot_1.png)

#### Microcontrolador com WiFi
![](imagens/Screenshot_3.png)

#### Carregador de Celular com cabo microusb
![](imagens/Screenshot_7.png)

### Itens opcionais e/ou acabamento do projeto

#### Tela de OLED
![](imagens/Screenshot_2.png)

#### Cabo para conexão com a Tela e adaptação do sensor
![](imagens/Screenshot_5.png)

#### Tubo para isolamento de emendas

> Ou use fita isolante

![](imagens/Screenshot_9.png)

#### Cabo para conextar o sensor da caixa ao controlador
![](imagens/Screenshot_13.png)

#### Pode ser utilizado cabo de rede, em vez de cobo anterior
![](imagens/Screenshot_14.png)

#### CAP 50mm Esgoto, com anel de vedação
![](imagens/Screenshot_10.png)
![](imagens/Screenshot_16.png)

> Nota: Se for utilizar o sensor impermeável, substituir esses dois itens e a redução de 50/25 por um cap de 25.

#### Redução 50/25mm água
![](imagens/Screenshot_11.png)

#### Pedaço de cano de 25mm
![](imagens/Screenshot_12.png)

#### Caixa de sobrepor com tampa cega
![](imagens/Screenshot_15.png)
Para instalar o LCD e acomodar o ESP32

## Execução do projeto

> O código fonte do programa está neste projeto.

- Copie o código do programa para o seu computador
- Instale a IDE Arduino, e instale as Bibliotecas Adafruit MQTT, Ultrassoinic, SSD1306
- Crie um bot no Telegram, crie um char e anote o token e o id do Chat
- Crie uma conta na Adafruit e anote o token
- Crie um site que tenha uma página que recebe dois parametros, Sendo ID do Sensor e Valor

#### A montagem das peças:

Fure o CAP de modo que o sensor encaixe bem justo
![](imagens/Screenshot_17.png)

Utilize algum material para servir de apoio ao sensor. Eu utilizei um pedaço de isopor
![](imagens/Screenshot_18.png)

Ajuste a altura pra sona não ficar muito pra fora
![](imagens/Screenshot_19.png)

Faça as emendas necessárias e ANOTE a sequencia correta dos fios, seguindo o codigo do projeto
![](imagens/Screenshot_23.png)

Encaise o cap, a redução e um pedaço de cano, com o já conectado ao sensor e fixe na caixa. A Sonda não deve encostar na água
![](imagens/Screenshot_20.png)

Com uma trena, meça quantos centimetros tem entre a sonda e o nivel maximo da água. Essa informação deve ser inserida no programa antes de gravar.
![](imagens/Screenshot_21.png)

Faça os acabamentos necessários. Esta etapa pode variar de caso a caso.
![](imagens/Screenshot_22.png)

Na casa de máquinas, ou onde for ficar o microcontrolador, faça as conexões conforme o projeto
![](imagens/Screenshot_24.png)

Imagem do armário na casa de máquinas.
![](imagens/Screenshot_25.png)

> É verdade, ficou bem feio mas vamos logo mais acomodar em uma caixa de sobrepor e prender a tela na tampa cega.

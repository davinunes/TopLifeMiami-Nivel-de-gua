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



## Material utilizado

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

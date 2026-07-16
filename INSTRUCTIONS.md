# Projeto: XPad Link

## Objetivo

Desenvolver um aplicativo **Windows (.exe)** que funcione como um **sampler sincronizado**, 
inspirado no **X-Pad do Pioneer RMX-1000**, utilizando o **Ableton Link** para sincronização 
com o **Serato DJ Pro**.

O programa **não deve depender do Ableton Live**. Ele deve participar diretamente da sessão 
**Ableton Link** criada pelo Serato.

# Objetivos principais

O software deverá:

- Ler o BPM atual do Serato DJ Pro através do Ableton Link.
- Ler a posição do beat (timeline).
- Permanecer sincronizado em tempo real caso o BPM seja alterado.
- Receber comandos MIDI de um controlador (LPD8 inicialmente).
- Disparar samples perfeitamente sincronizados com o grid do Serato.
- Fazer time stretching dos samples para acompanhar mudanças de BPM sem alterar o pitch.
- Possuir baixa latência.
- Ser "sample accurate", utilizando o relógio do Link e o callback da engine de áudio.

---

# Arquitetura

```text
                 Serato DJ Pro
                       │
                 Ableton Link
                       │
        ┌──────────────┴──────────────┐
        │                             │
    Link Session                 Beat Timeline
        │                             │
        └──────────────┬──────────────┘
                       │
                Beat Scheduler
                       │
      ┌────────────────┼─────────────────┐
      │                │                 │
   MIDI Input      Sample Engine     GUI
      │                │
      │          Time Stretch
      │                │
      └────────────► Audio Engine
                       │
                WASAPI / ASIO
                       │
                    Output
```

---

# Tecnologias sugeridas

## Linguagem

C++20

Motivos:

- SDK oficial do Ableton Link
- Baixa latência
- Controle total da engine de áudio
- Excelente integração com MIDI

---

## Build

- CMake

---

## Interface

Opções:

- Dear ImGui (preferencial)
- JUCE (caso seja necessária uma interface mais robusta)

---

## Áudio

Preferencialmente:

- miniaudio

Alternativas:

- JUCE Audio
- PortAudio

---

## MIDI

RtMidi

Responsável por:

- Detectar dispositivos MIDI
- Ler Note On
- Ler Note Off
- Ler Control Change
- Permitir MIDI Learn futuramente

---

## Time Stretch

Bibliotecas recomendadas:

- Rubber Band Library
- Signalsmith Stretch

Objetivo:

Modificar a duração do sample sem alterar seu pitch.

---

## Configuração

JSON

Biblioteca:

nlohmann/json

---

# Ableton Link

O programa utilizará o SDK oficial do Ableton Link.

Ele deverá:

Entrar automaticamente em qualquer sessão Link existente.

Caso o Serato esteja com Link ativado:

Receber continuamente:

- BPM
- Beat
- Estado da sessão

Não será necessário:

- Ableton Live
- DAW
- Plugins

O programa participa apenas da sessão Link.

---

# Fluxo do programa

## Inicialização

- Inicializar engine de áudio.
- Inicializar Ableton Link.
- Inicializar entrada MIDI.
- Carregar configurações.
- Carregar banco de samples.

---

## Loop principal

Atualizar continuamente:

- BPM
- Beat atual
- Estado do Link

---

## Entrada MIDI

Ao receber um Note On:

Nunca tocar imediatamente.

Fluxo:

```

Pad pressionado
↓
Ler beat atual
↓
Calcular próximo ponto válido
↓
Agendar disparo
↓
Audio callback executa

```

---

# Scheduler

Este é o componente mais importante do projeto.

Nunca utilizar:

```cpp
playSample();
```

diretamente após pressionar o pad.

O Scheduler deverá trabalhar em tempo musical.

Exemplo:

```

Beat atual

542.32

Pad configurado para 1/4

↓

Agendar:

542.50

```

O Scheduler será responsável por:

- Quantização
- Precisão
- Sincronização

---

# Audio Callback

Toda reprodução deverá acontecer dentro do callback da engine de áudio.

Nunca utilizar:

- Sleep()
- Timers do Windows
- std::this_thread::sleep_for()

O callback deverá consultar continuamente:

- Beat atual
- Eventos agendados

Executando exatamente quando necessário.

---

# Banco de Samples

Estrutura sugerida:

```

samples/

snare/
1_4.wav
1_8.wav
1_16.wav

kick/

clap/

fx/

```

Cada sample possui um BPM de referência.

Exemplo:

```

130 BPM

```

---

# Ajuste automático de BPM

Quando o BPM mudar:

```

Sample

130 BPM

↓

Serato

126 BPM

↓

PlaybackRate

126 / 130

↓

Time Stretch

↓

Mesmo pitch

```

---

# Quantização

Cada pad poderá possuir um modo de quantização.

Exemplos:

- 1/1
- 1/2
- 1/4
- 1/8
- 1/16
- 1/32

Exemplo:

Beat atual:

```

420.18

```

Modo:

```

1/8

```

Próximo disparo:

```

420.25

```

---

# Modos dos Pads

Cada pad poderá operar em:

## One Shot

Toca apenas uma vez.

---

## Loop

Enquanto permanecer pressionado.

---

## Retrigger

Reinicia o sample a cada subdivisão.

---

## Hold

Continua repetindo até soltar.

---

# Mapeamento MIDI

Inicialmente:

Controlador:

Akai LPD8

Mapeamento:

Pad 1

→ Snare 1/4

Pad 2

→ Snare 1/8

Pad 3

→ Snare 1/16

Posteriormente:

Adicionar:

MIDI Learn.

---

# Knobs

Os knobs poderão controlar:

- Volume
- LPF
- HPF
- Delay
- Reverb
- Dry/Wet
- Pitch
- Gate

Todos configuráveis.

---

# Interface

A interface deverá exibir:

## Link

- Conectado
- Desconectado

---

## BPM

Exemplo:

```

126.00 BPM

```

---

## Beat

Exemplo:

```

1532.42

```

---

## Banco ativo

Exemplo:

```

Tech House

```

---

## Pads

Visualização dos pads.

Quando um pad for pressionado:

Animar.

Quando estiver agendado:

Exibir indicação.

---

# Configuração

Salvar:

- Banco ativo
- Volume
- Mapeamento MIDI
- Quantização
- Último dispositivo MIDI
- Última saída de áudio

---

# Estrutura sugerida

```

src/

audio/
AudioEngine.cpp
AudioScheduler.cpp
TimeStretch.cpp

link/
LinkManager.cpp

midi/
MidiManager.cpp

samples/
Sample.cpp
SampleBank.cpp

gui/
MainWindow.cpp

config/
Config.cpp

main.cpp

```

---

# Prioridade de desenvolvimento

## Fase 1

- Inicializar Link
- Receber BPM
- Receber Beat

---

## Fase 2

- Engine de áudio
- Reprodução simples de WAV

---

## Fase 3

- Entrada MIDI

---

## Fase 4

- Scheduler sincronizado

---

## Fase 5

- Quantização

---

## Fase 6

- Time Stretch

---

## Fase 7

- Interface

---

## Fase 8

- Configuração

---

# Melhorias futuras

- Múltiplos bancos de samples.
- MIDI Learn.
- Arrastar arquivos WAV para os pads.
- Sincronização de efeitos.
- Delay sincronizado.
- Reverb sincronizado.
- Swing.
- Nudge.
- Reverse.
- Roll.
- Gate.
- Exportar presets.
- Importar presets.
- Interface customizável.
- Skins.

---

# Requisitos de qualidade

O software deve:

- Ser extremamente responsivo.
- Não utilizar timers comuns para sincronização.
- Basear toda a reprodução na timeline do Ableton Link.
- Ser estável durante mudanças de BPM.
- Não perder sincronização após horas de uso.
- Ter arquitetura modular.
- Permitir expansão futura para novos efeitos e novos tipos de samples.

---

# Resultado esperado

Ao iniciar o Serato DJ Pro com Ableton Link ativado e executar este aplicativo:

1. O aplicativo detecta automaticamente a sessão Link.
2. Exibe BPM e beat em tempo real.
3. O usuário pressiona um pad do LPD8.
4. O disparo é quantizado conforme a configuração.
5. O sample é reproduzido exatamente no tempo musical correto.
6. Se o DJ alterar o BPM, o sample acompanha automaticamente mantendo a sincronização e o pitch.
7. O comportamento percebido pelo usuário deve ser equivalente ao X-Pad do Pioneer RMX-1000, porém implementado inteiramente em software para Windows.
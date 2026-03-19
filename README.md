# Gerenciador MIL Traducoes

Homebrew para Nintendo Switch focado em distribuir e instalar traducoes, dublagens, mods, cheats e save games a partir de um catalogo remoto.

## Base atual

- projeto em `C++17` com `devkitPro`, `libnx`, `libcurl` e `libarchive`
- build por `CMake` com `Switch.cmake`
- UI grafica em framebuffer com navegacao por controle
- catalogo remoto com cache local automatico em `sdmc:/config/mil-manager/cache/index.json`
- instalacao de pacotes ZIP para `sdmc:/`
- recibos de instalacao para remocao limpa em `sdmc:/config/mil-manager/receipts`
- configuracao em `sdmc:/config/mil-manager/settings.ini`
- deteccao de titulos instalados no console por `ns`
- sincronizacao host-side para Ryujinx

## Build

```powershell
cmake -S . -B build-switch -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=C:/devkitPro/cmake/Switch.cmake -DDEVKITPRO=C:/devkitPro
cmake --build build-switch
```

## Catalogo remoto

O app aceita:

- `https://.../index.json`
- GitHub Pages
- `raw.githubusercontent.com`
- links publicos de arquivo do `mega.nz` para `index.json`
- links publicos de pasta do `mega.nz`

Fluxo em runtime:

1. tenta baixar pelas URLs configuradas em `settings.ini`
2. se der certo, atualiza o cache local em `sdmc:/config/mil-manager/cache/index.json`
3. se a rede falhar, usa esse cache local como fallback

## Fonte central do indice

Edite [catalog-source.json](/Users/lordd/source/codex/mil-manager/catalog-source/catalog-source.json) e gere o indice final com:

```powershell
python tools\generate-index.py
```

Saida:

- [index.json](/Users/lordd/source/codex/mil-manager/dist/index.json)
- [site/index.json](/Users/lordd/source/codex/mil-manager/site/index.json) apos preparar o site do GitHub Pages

Campos de controle no topo do indice:

- `catalogName`
- `channel`
- `schemaVersion`
- `catalogRevision`
- `generatedAt`

## GitHub Pages

Fluxo recomendado para um endpoint estavel:

1. editar [catalog-source.json](/Users/lordd/source/codex/mil-manager/catalog-source/catalog-source.json)
2. gerar o catalogo com `python tools\generate-index.py`
3. preparar o site do Pages com `python tools\prepare-pages-site.py`
4. publicar pelo workflow [publish-catalog-pages.yml](/Users/lordd/source/codex/mil-manager/.github/workflows/publish-catalog-pages.yml)

O workflow gera o catalogo, monta a pasta `site/` e publica:

- `https://SEU_USUARIO.github.io/SEU_REPOSITORIO/index.json`

Se voce configurar dominio proprio no GitHub Pages, o endpoint final pode ficar assim:

- `https://catalogo.miltraducoes.com/index.json`

Se quiser que o workflow escreva um `CNAME`, configure a variavel de repositorio `MIL_PAGES_CNAME`.

Arquivos principais desse fluxo:

- [prepare-pages-site.py](/Users/lordd/source/codex/mil-manager/tools/prepare-pages-site.py)
- [publish-catalog-pages.yml](/Users/lordd/source/codex/mil-manager/.github/workflows/publish-catalog-pages.yml)
- [settings.github-pages.example.ini](/Users/lordd/source/codex/mil-manager/docs/settings.github-pages.example.ini)

## settings.ini

Exemplo:

```ini
language=auto
scan_mode=auto
catalog_url=https://SEU_USUARIO.github.io/SEU_REPOSITORIO/index.json
catalog_url=https://raw.githubusercontent.com/SEU_USUARIO/SEU_REPOSITORIO/main/dist/index.json
```

Valores de `scan_mode`:

- `auto`: usa importacao do emulador quando existir; caso contrario usa modo seguro no console
- `full`: tenta listar todos os jogos instalados pelo `ns` do console
- `catalog`: sonda apenas os `titleId` presentes no catalogo
- `off`: nao tenta detectar jogos instalados

## Ryujinx

O homebrew rodando dentro do emulador nao enxerga automaticamente as pastas da biblioteca do host. Por isso, o fluxo recomendado para Ryujinx e sincronizar o catalogo e os titulos instalados antes de abrir o app.

Sincronizacao manual:

```powershell
powershell -ExecutionPolicy Bypass -File tools\sync-ryujinx.ps1
```

Com URL explicita:

```powershell
powershell -ExecutionPolicy Bypass -File tools\sync-ryujinx.ps1 -CatalogUrl https://SEU_USUARIO.github.io/SEU_REPOSITORIO/index.json
```

Launcher com sincronizacao automatica:

```powershell
powershell -ExecutionPolicy Bypass -File tools\start-ryujinx-with-sync.ps1
```

Arquivos gerados na SD virtual:

- `sdmc:/switch/mil_manager/index.json`
- `sdmc:/switch/mil_manager/emulator-installed.json`

## Compatibilidade de versao

Como o `LayeredFS` nao separa por versao do jogo, o catalogo deve informar compatibilidade por faixa ou por versoes exatas:

- `minGameVersion`
- `maxGameVersion`
- `exactGameVersions`

No momento da instalacao, o app grava a versao do jogo em uso no recibo local para futuras auditorias e remocao.

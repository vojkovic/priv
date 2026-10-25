# priv

An extremely minimal metasearch engine in C.

Even works completely without JS except for the autocomplete and infinite scroll.

I have been a bit disappointed with the memory usage and speed of searxng (i.e. such as when ranking many results, the server can take even over 200ms!). This is probably due to the nature of an interpreted language and the fact that SXNG has many, many features (which is not even a bad thing).

For priv, I have put performance at the forefront. I have made many micro optimisations and also gave the UI a small refresh in a way I've always wanted to do but never had the tools to do it. More updates will be on the way.

This whole project has been made possible through the use of LLMs (is vibecoding the nomenclature?), so buyers please beware! This is still very much in beta and may have bugs.

## Run

    docker build -t priv .
    docker run -d -p 127.0.0.1:8888:8888 -v priv-cache:/cache priv

## Configure

Make priv yours!

You can edit the `config.toml` to change things such as the instance name, listen address, timeout, bot detection and which engines are on.

    curl -o config.toml https://raw.githubusercontent.com/vojkovic/priv/master/config.toml
    docker run -d -p 127.0.0.1:8888:8888 -v priv-cache:/cache -v ./config.toml:/etc/priv/config.toml:ro priv

## Develop

    make format
    make format-check
    make lint          # clang-tidy and cppcheck
    make docker-check  # format-check and lint inside the build image

The `tools` stage of Dockerfile can also be used e.g:

    docker build --target tools -t priv-tools .
    docker run --rm -v .:/src -w /src priv-tools make format lint

## Search syntax

Most of the bangs are supported:

- `!g`, `!br`, `!yt` (or `!google_images` by name) (and `!!g` to redirect)
- `!images`, `!videos`, `!news`, `!music`, `!tech`, `!research` a whole category
- `:de`, `:en-AU`, `:german`, `:deutschland` the search language
- `<3` a timeout of 3 seconds max

## Engines so far

| category | engines |
| --- | --- |
| general | google, google docs, bing, google cse, brave, duckduckgo, duckduckgo web, dogpile, privacywall, oceanhero, swisscows, iseek, s1search, wikipedia |
| images | google images, google cse images, brave images, duckduckgo images, dogpile images |
| videos | google videos, duckduckgo videos, youtube, brave videos, bing videos, dailymotion |
| news | google news, duckduckgo news, reuters, brave news, bing news, wikinews |
| music | soundcloud, wikicommons audio, genius, mixcloud, radio browser |
| tech | arch linux wiki, mdn, mankier, pypi, stackoverflow, askubuntu, superuser |
| research | arxiv, europepmc, pdbe, pubmed, openalex, crossref, google scholar |

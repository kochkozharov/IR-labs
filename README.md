# IR-labs — поисковая система

## Запуск

```bash
# Сбор корпуса из Википедии (однократно)
docker compose run --rm scraper

# Сбор корпуса из КиберЛенинки (однократно)
docker compose run --rm scraper-cyberleninka

# Запуск движка и веб-интерфейса (HTTP-режим)
docker compose up engine frontend
```

Веб-интерфейс: http://localhost:8080

```bash
# CLI-режим (интерактивный поиск в терминале)
docker compose run --rm engine /app/engine
```

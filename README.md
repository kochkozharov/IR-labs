# IR-labs — поисковая система

## Запуск

```bash
# Сбор корпуса (однократно)
docker compose run --rm scraper

# Запуск движка и веб-интерфейса (HTTP-режим)
docker compose up engine frontend
```

Веб-интерфейс: http://localhost:8080

```bash
# CLI-режим (интерактивный поиск в терминале)
docker compose run --rm engine /app/engine
```

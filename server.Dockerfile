FROM ubuntu:18.04

WORKDIR /app

COPY server/bin server/bin
COPY data data

RUN chmod +x server/bin/game_server_prod

CMD ["server/bin/game_server_prod", "maps/map1.json", "--production"]

FROM node:12.20.1-stretch

RUN apk add --no-cache bash

ADD package.json /app/
WORKDIR /app
RUN npm install --production

COPY . .

RUN chmod +x server/bin/game_server_prod

CMD ["npm", "run", "run-both"]
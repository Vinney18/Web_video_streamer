FROM mcr.microsoft.com/dotnet/sdk:7.0 AS base
EXPOSE 8890

FROM node:18.13 as node-build
COPY . /app

WORKDIR /app

ENTRYPOINT ["./PlayerServer"]
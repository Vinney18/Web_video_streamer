FROM mcr.microsoft.com/dotnet/aspnet:7.0 AS base
COPY . /app
WORKDIR /app
# Set environment variables for .NET
ENV DOTNET_ROOT=/usr/share/dotnet
ENV PATH=$PATH:/root/.dotnet/tools:$DOTNET_ROOT
EXPOSE 8890
ENTRYPOINT ["./PlayerServer"]
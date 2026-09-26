FROM ubuntu:20.04 as release

RUN apt-get update && apt-get install -y libavformat58 libavcodec58 libswresample3 libswscale5 libavutil56 libavdevice58 libavfilter7 libpostproc55 ffmpeg
COPY . /app/
WORKDIR /app

EXPOSE 8181
CMD ./videorelay -s1
FROM ubuntu:20.04 as release

RUN apt-get update && apt-get install -y libavformat57 libavcodec57 libswresample2 libswscale4 libavutil55 libavdevice57 libavfilter6 libpostproc54 ffmpeg
RUN mkdir /home/app 
COPY . /app/
WORKDIR /app

EXPOSE 8181
CMD ./streamer -s1
FROM ubuntu:jammy
RUN apt-get update -y
RUN apt-get install wget unzip libssl-dev libdb++-dev libboost-all-dev -y
WORKDIR /opt/
RUN wget https://github.com/Crypto-Currency/SpiderByte/raw/refs/heads/master/release/2204-spiderbyted
RUN chmod +x 2204-spiderbyted
RUN mv 2204-spiderbyted /usr/bin/spiderbyted
RUN wget https://raw.githubusercontent.com/TheRetroMike/rmt-nomp/master/scripts/blocknotify.c
RUN gcc blocknotify.c -o /usr/bin/blocknotify
CMD /usr/bin/spiderbyted --daemon;tail -f /root/.SpiderByte/debug.log

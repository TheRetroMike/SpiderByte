FROM ubuntu:jammy
RUN apt-get update -y
RUN apt-get install wget unzip libssl-dev libdb++-dev libboost-all-dev -y
WORKDIR /opt/
RUN wget https://github.com/Crypto-Currency/SpiderByte/raw/refs/heads/master/release/2204-spiderbyted
RUN chmod +x 2204-spiderbyted
RUN mv 2204-spiderbyted /usr/bin/spiderbyted
CMD /usr/bin/spiderbyted --daemon

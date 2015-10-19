FROM alpine:latest
MAINTAINER Tom Denham <tom@projectcalico.org>

ADD dist/bird /usr/local/bin/bird
ADD dist/bird6 /usr/local/bin/bird6
ADD dist/birdcl /usr/local/bin/birdcl

# Provide just enough config for bird to start.
#RUN mkdir /config
#RUN echo "protocol device {}" > /config/bird2.cfg

# The /config volume should map --volume-from calico-confd
#VOLUME /config

CMD ["birdcl", "-s", "/bird.ctl"]


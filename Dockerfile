FROM alpine:latest
MAINTAINER Tom Denham <tom@projectcalico.org>

ADD . /code
WORKDIR /code

RUN /code/build.sh
RUN mkdir -p /etc/calico/confd/config/
RUN echo "protocol device {}" > /etc/calico/confd/config/bird.cfg
RUN echo "protocol device {}" > /etc/calico/confd/config/bird6.cfg

CMD ["bird", "-R", "-s", "bird.ctl", "-d", "-c", "/etc/calico/confd/config/bird.cfg"]

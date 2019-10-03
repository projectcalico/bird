FROM alpine:edge

ADD birdy/bird-wrapper.sh /opt/bird-wrapper.sh
ADD birdy/bird.conf /etc/bird.conf
ADD dist/amd64/bird /usr/sbin/bird
ADD dist/amd64/birdcl /usr/bin/birdcl
ADD birdy/bird6.conf /etc/bird6.conf
ADD dist/amd64/bird6 /usr/sbin/bird6
ADD dist/amd64/birdcl6 /usr/bin/birdcl6

RUN mkdir -p /etc/bird
RUN mkdir -p /etc/bird6
RUN mkdir -p /usr/local/var/run
RUN mkdir -p /usr/local/etc
RUN ln -s /etc/bird.conf /usr/local/etc/bird.conf
RUN ln -s /etc/bird6.conf /usr/local/etc/bird6.conf

ENTRYPOINT ["/opt/bird-wrapper.sh"]
CMD ["/bin/sh"]
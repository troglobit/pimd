FROM debian:stretch

COPY . /root/pimd
WORKDIR /root/pimd
RUN apt-get update && apt-get install -y build-essential automake
RUN ./autogen.sh
RUN ./configure --prefix=/usr --sysconfdir=/etc
RUN make

FROM debian:stretch
COPY --from=0 /root/pimd/src/pimd /root/pimd/src/pimctl /usr/sbin/

CMD [ "/usr/sbin/pimd", "--foreground" ]

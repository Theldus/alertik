FROM scratch
COPY alertik /alertik
EXPOSE 5140/udp
CMD ["/alertik"]

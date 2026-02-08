FROM gcc:13

WORKDIR /build

RUN apt-get update && apt-get install -y cmake wget

RUN wget -q https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.14.3/httplib.h \
    -O /usr/local/include/httplib.h

COPY CMakeLists.txt .
COPY src/ src/

RUN cmake . && make

WORKDIR /app

RUN cp /build/engine /app/engine && \
    mkdir -p /app/data /app/output

EXPOSE 9090

CMD ["/app/engine", "--serve", "--port", "9090"]

FROM alpine

RUN apk add --no-cache gcc cmake make musl-dev linux-headers

COPY . /dmon

WORKDIR /dmon/build

RUN cmake -DCMAKE_BUILD_TYPE="Debug" .. && \
    cmake --build .

CMD ctest .

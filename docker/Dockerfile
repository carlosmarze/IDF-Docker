FROM espressif/idf:release-v6.0

RUN useradd -ms /bin/bash esp && \
    usermod -aG dialout esp

USER esp
WORKDIR /project

ENV IDF_PATH=/opt/esp/idf
ENV PATH="$IDF_PATH/tools:$PATH"

ENTRYPOINT ["/bin/bash", "/project/docker/entrypoint.sh"]

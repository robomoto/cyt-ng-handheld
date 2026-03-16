FROM espressif/idf:v5.2

# Install additional tools for development
RUN apt-get update && apt-get install -y \
    clang-format \
    cppcheck \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /project

# Entry point runs idf.py by default
ENTRYPOINT ["/opt/esp/entrypoint.sh"]
CMD ["idf.py", "build"]

FROM base

# Do not push image !!!!! For local usage only
ARG SSH_PRIVATE_KEY
RUN mkdir /root/.ssh/
RUN echo "${SSH_PRIVATE_KEY}" > /root/.ssh/id_rsa
RUN chmod 600 /root/.ssh/id_rsa

# add credentials on build
ARG SSH_PUBLIC_KEY
RUN echo "${SSH_PUBLIC_KEY}" > /root/.ssh/id_rsa.pub
RUN chmod 644 /root/.ssh/id_rsa.pub

# Optional as you can manually add these
ARG SSH_KNOWN_HOSTS
RUN echo "${SSH_KNOWN_HOSTS}" > /root/.ssh/known_hosts
RUN chmod 644 /root/.ssh/known_hosts

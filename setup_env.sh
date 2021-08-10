# Run source ./setup_env.sh
export PATH=$PATH:${PWD}/tools:${PWD}/bench/runners


RelCMake() {
    cmake -DCMAKE_BUILD_TYPE=Release "$@"
}

DebCMake() {
    cmake -DCMAKE_BUILD_TYPE=Debug "$@"
}

SanCMake() {
    cmake -DCMAKE_BUILD_TYPE=SanitizedDebug "$@"
}

CovCMake() {
    cmake -DCMAKE_BUILD_TYPE=Coverage "$@"
}

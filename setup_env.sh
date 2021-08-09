# Run source ./setup_env.sh
export PATH=$PATH:${PWD}/tools:${PWD}/bench/runners


alias RelCMake=cmake -DCMAKE_BUILD_TYPE=Release
alias DebCMake=cmake -DCMAKE_BUILD_TYPE=Debug
alias SanCMake=cmake -DCMAKE_BUILD_TYPE=SanitizedDebug

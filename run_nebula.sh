ZIP_FILE=$1
TARGET=$2
LOG_FILE=nebula.log
LOG_SMALL_BENCH=nebula_small_bench.log
DATA_DIR=data

function copy_to_nebula {
    scp $ZIP_FILE nebula:~/test/$ZIP_FILE
}

function copy_from_nebula {
    mkdir -p $DATA_DIR
    scp -r 'nebula:~/test/uut/data/*' $DATA_DIR
}

function clean_test_dir {
    ssh nebula "mkdir -p test"
    ssh nebula "\
        cd test
        rm -rf *"
}

#/usr/local/slurm/bin/srun -t 1 -p q_thesis make $TARGET
function run_on_nebula {
    ssh nebula "\
        cd test
        unzip -u $ZIP_FILE -d uut &&
        cd uut &&
        make

        srun -t 1 -p q_student make $TARGET
        while "'[ ! $(squeue -u $(whoami) | wc -l) = 1 ]'"; do
            squeue
            sleep 1
        done

        echo 'done'" | tee $LOG_FILE
}

if [ ! $# = 2 ]; then
    echo "Error: first argument has to be a .zip archive"
    echo "Error: second argument has to be a valid make target, i.e., small-bench"
    exit 1
fi

clean_test_dir      &&
copy_to_nebula      &&
run_on_nebula       &&
copy_from_nebula

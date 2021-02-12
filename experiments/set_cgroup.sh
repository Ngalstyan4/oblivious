gname="run_c"
rmdir "/cgroup2/$gname"
mkdir "/cgroup2/$gname"
echo 55m > "/cgroup2/$gname/memory.high"
chown -R narekg:narekg "/cgroup2/$gname/"
# cgroup v1:
# rmdir /sys/fs/cgroup/memory/mmult_eigen
# mkdir /sys/fs/cgroup/memory/mmult_eigen
# echo 256k > /sys/fs/cgroup/memory/mmult_eigen/memory.limit_in_bytes
# chown narekg:narekg /sys/fs/cgroup/memory/mmult_eigen/tasks

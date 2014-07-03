cov-build --dir cov-int gcc -o jfbv jfbv.c -ljpeg
tar czvf jfbv.tgz cov-int
curl --form project=JoakimLarsson%2Fjfbv --form token=O9BjR4IcH7NBKRz6zCi2Dw --form email=joakim@bildrulle.nu --form file=@./jfbv.tgz --form version="Version" --form description="Description"  https://scan.coverity.com/builds?project=JoakimLarsson%2Fjfbv
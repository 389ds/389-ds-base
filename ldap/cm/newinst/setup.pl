#!./tools/perl
# Author: Nathan Kinder
#
# This program will package a downloaded JRE into a nsjre.zip
# file suitable for a DS install.

use lib './lib';
use FileHandle;

autoflush STDERR 1;
autoflush STDOUT 1;

delete $ENV{LD_LIBRARY_PATH};

# Set required JRE version
if ($^O eq "hpux") {
    $jdkVersion = "HP's 32-bit HP-UX";
    $reqVersion = "1.4.2.04";
    delete $ENV{SHLIB_PATH};
} elsif ($^O eq "MSWin32") {
    $jdkVersion = "Sun's 32-bit MS Windows";
    $reqVersion = "1.4.2_05";
} elsif ($^O eq "linux") {
    $jdkVersion = "Sun's 32-bit Linux";
    $reqVersion = "1.4.2_05";
} elsif ($^O eq "solaris") {
    $jdkVersion = "Sun's 32-bit Solaris";
    $reqVersion = "1.4.2_05";
} else {
    print("Unsupported operating system: $^O!\n");
    exit;
}

# Check if base/nsjre.zip or base/jre.z already exists
unless (-e "./base/nsjre.zip" || -e "./base/jre.z") {
    # Check if NSJRE environment variable is set
    if ($ENV{NSJRE}) {
        chomp ($jrepath = $ENV{NSJRE});
        print ("Using NSJRE environment variable: $jrepath\n");
    } else {
        print ("In order to run setup, you need to have version");
        print (" $reqVersion of\n");
        print ("$jdkVersion Java runtime environment on your system.\n\n");
        print ("Enter the path to the unpackaged JRE: ");
        chomp ($jrepath = <STDIN>);
    }

    VerifyJRE();
    CreatePackage();
    CleanUp();
}

# Kick off setup
exec("./dssetup @ARGV");

sub VerifyJRE {
    print ("\nVerifying JRE...");
    unless (-e "$jrepath" && -r "$jrepath") { die ("\nError: Can't access JRE: $!\n"); }
    unless (-e "$jrepath/bin" && -r "$jrepath/bin") { die ("\nError: Can't access $jrepath/bin: $!\n"); }
    unless (-e "$jrepath/bin" && -r "$jrepath/lib") { die ("\nError: Can't access $jrepath/lib: $!\n"); }
    unless (-e "$jrepath/bin/java" || -e "$jrepath/bin/java.exe") { die ("\nError: Invalid JRE found: $!\n"); }

    my $jreVersion = `\"$jrepath/bin/java\" -version 2>&1`;
    $jreVersion =~ /".*"/;
    $foundVersion = $&;
    print (" Found JRE $foundVersion\n");
    unless ($foundVersion =~ $reqVersion) {
        print ("\nWarning: This product was certified with JRE version \"$reqVersion\".  You have version $foundVersion.\n");
        print ("The product may not behave correctly if you use this JRE.\n");
        print ("Would you like to continue anyway [yes/no]? ");
        chomp ($answer = <STDIN>);
        unless ($answer eq "yes") { exit; } 
    }
}

sub CreatePackage {
    print ("Creating JRE package...");

    # Create packaging area
    mkdir ("bin", 0755) || die ("Error: Can't create ./bin: $!\n");
    mkdir ("bin/base", 0755) || die ("Error: Can't create ./bin/base: $!\n");
    mkdir ("bin/base/jre", 0755) || die ("Error: Can't create ./bin/base/jre: $!\n");

    # Copy bin and lib from JRE into packaging area, then create zip archive
    if ($^O eq "MSWin32") {
        system ("xcopy /E /I /Q \"$jrepath/bin\" \"bin/base/jre/bin\"") == 0 ||
            die ("\nError: Can't copy JRE: $!\n");
        system ("xcopy /E /I /Q \"$jrepath/lib\" \"bin/base/jre/lib\"") == 0 ||
            die ("\nError: Can't copy JRE: $!\n");
        system ("./tools/zip -q -r ./base/jre.z ./bin") == 0 ||
            die ("\nError: Can't create JRE archive: $!\n");
    } else {
        system ("cp -R $jrepath/bin ./bin/base/jre") == 0 ||
            die ("\nError: Can't copy JRE: $!\n");
        system ("cp -R $jrepath/lib ./bin/base/jre") == 0 ||
            die ("\nError: Can't copy JRE: $!\n");

        # On HP-UX, we need to move some libraries in the JRE package
        if ($^O eq "hpux") {
            system ("cp -f ./bin/base/jre/lib/PA_RISC/native_threads/libhpi.sl ./bin/base/jre/lib/PA_RISC/libhpi.sl") == 0 ||
                die ("\nError: Can't create JRE archive: $!\n");
            system ("cp -f ./bin/base/jre/lib/PA_RISC2.0/native_threads/libhpi.sl ./bin/base/jre/lib/PA_RISC2.0/libhpi.sl") == 0 ||
                die ("\nError: Can't create JRE archive: $!\n");
            system ("cp -f ./bin/base/jre/lib/PA_RISC2.0W/native_threads/libhpi.sl ./bin/base/jre/lib/PA_RISC2.0W/libhpi.sl") == 0 ||
                die ("\nError: Can't create JRE archive: $!\n");
        }

        system ("./tools/zip -q -r ./base/nsjre.zip ./bin") == 0 ||
            die ("\nError: Can't create JRE archive: $!\n");
    }

    print (" Done\n");
}

sub CleanUp {
    print ("Cleaning up...");

    # Remove packaging area
    RemoveFiles ("./bin");
    rmdir ("./bin") || die ("Error: can't remove ./bin: $!\n");

    print (" Done\n");
}

sub RemoveFiles {
    my $dir = shift;
    opendir (DIR, $dir) || die ("Error: Can't open $dir: $!");
    my @entries = map { "$dir/$_" } grep { !/^\.$|^\.\.$/ } readdir DIR;
    closedir DIR;
    for (@entries) {
	if (-l $_) {
            unlink || die ("Error: Can't remove $_: $!\n");
	} elsif (-d $_) {
            RemoveFiles($_);
            rmdir($_) || die ("Error: Can't remove $_: $!\n");
        } else {
            unlink || die ("Error: Can't remove $_: $!\n");
        }
    }
}


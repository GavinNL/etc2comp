
package main

import (
    "bytes"
    "flag"
    "fmt"
    "io"
    "io/ioutil"
    "log"
    "os"
    "os/exec"
)

const chunkSize = 64000

func deepCompare(file1, file2 string) bool {
    // Check file size ...

    f1, err := os.Open(file1)
    if err != nil {
        log.Fatal(err)
    }

    f2, err := os.Open(file2)
    if err != nil {
        log.Fatal(err)
    }

    for {
        b1 := make([]byte, chunkSize)
        _, err1 := f1.Read(b1)

        b2 := make([]byte, chunkSize)
        _, err2 := f2.Read(b2)

        if err1 != nil || err2 != nil {
            if err1 == io.EOF && err2 == io.EOF {
                return true
            } else if err1 == io.EOF || err2 == io.EOF {
                return false
            } else {
                log.Fatal(err1, err2)
            }
        }

        if !bytes.Equal(b1, b2) {
            return false
        }
    }
}

func main() {

    flag.Parse()

    arguments := flag.Args()
    executable := arguments[1]
    image := arguments[2]
    expected := arguments[3]

    file, err := ioutil.TempFile(os.TempDir(), "EtcToolTest")
    defer os.Remove(file.Name())

    cmd := exec.Command(
        executable,
        image,
        "-output", file.Name(),
    )
    stderr, err := cmd.StderrPipe()
    if err != nil {
        log.Fatalf("Retrieving error pipe failed: %s", err)
    }

    err = cmd.Start();
    if err != nil {
        log.Fatalf("Starting process failed: %s", err)
    }

    err = cmd.Wait();
    if err != nil {
        slurp, _ := ioutil.ReadAll(stderr)
        fmt.Errorf("%s\n", slurp)
        log.Fatal(err)
    }

    if !deepCompare(file.Name(), expected) {
        log.Fatalf("Expected output %s not produced", expected)
    }
}

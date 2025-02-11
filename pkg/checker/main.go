package main

import (
    "fmt"
    "os"
    "sync"
    "strings"
    "os/exec"
    "bufio"

    "github.com/intel/goresctrl/pkg/rdt"
)


// internal package variables
var (
	intelRdtRootLock sync.Mutex
	intelRdtRoot     string
)

// NotFoundError represents not found error
type NotFoundError struct {
	ResourceControl string
}

func (e *NotFoundError) Error() string {
	return fmt.Sprintf("mountpoint for %s not found", e.ResourceControl)
}

// NewNotFoundError returns new error of NotFoundError
func NewNotFoundError(res string) error {
	return &NotFoundError{
		ResourceControl: res,
	}
}

// IsNotFound returns if notfound error happened
func IsNotFound(err error) bool {
	if err == nil {
		return false
	}
	_, ok := err.(*NotFoundError)
	return ok
}


func CheckResctrlSupport() {
	fmt.Println("CheckResctrlSupport invoked")
    if !mountResctrl(){
        err := rdt.Initialize("resctrl")
        if err != nil {
            fmt.Println("Error:", err)
            os.Exit(1)
        } 
    }
	fmt.Println("goresctrl is operational")
    os.Exit(0)
}

func mountResctrl() bool {
    if !isIntelRdtMounted(){
		fmt.Println("Resctrl not mounted, hence mounting it")
        // mount -t resctrl resctrl /sys/fs/resctrl
        if err := os.MkdirAll("/sys/fs/resctrl", 0755); err != nil {
			fmt.Println("Error occured while creating /sys/fs/resctrl directory")
            return false
        }
        if err := exec.Command("mount", "-t", "resctrl", "resctrl", "/sys/fs/resctrl").Run(); err != nil {
            return false
        }
        return true
    }
    return true
}

// IsIntelRdtMounted give true/false of RDT mounted or not
func isIntelRdtMounted() bool {
	_, err := getIntelRdtRoot()
	if err != nil {
		return false
	}
	return true
}

// Gets the root path of Intel RDT "resource control" filesystem
func getIntelRdtRoot() (string, error) {
	intelRdtRootLock.Lock()
	defer intelRdtRootLock.Unlock()

	if intelRdtRoot != "" {
		return intelRdtRoot, nil
	}

	root, err := findIntelRdtMountpointDir()
	if err != nil {
		return "", err
	}

	if _, err := os.Stat(root); err != nil {
		return "", err
	}

	intelRdtRoot = root
	return intelRdtRoot, nil
}

// Return the mount point path of Intel RDT "resource control" filesysem
func findIntelRdtMountpointDir() (string, error) {
	f, err := os.Open("/proc/self/mountinfo")
	if err != nil {
		return "", err
	}
	defer f.Close()
	s := bufio.NewScanner(f)
	for s.Scan() {
		text := s.Text()
		fields := strings.Split(text, " ")
		// Safe as mountinfo encodes mountpoints with spaces as \040.
		index := strings.Index(text, " - ")
		postSeparatorFields := strings.Fields(text[index+3:])
		numPostFields := len(postSeparatorFields)

		// This is an error as we can't detect if the mount is for "Intel RDT"
		if numPostFields == 0 {
			return "", fmt.Errorf("Found no fields post '-' in %q", text)
		}

		if postSeparatorFields[0] == "resctrl" {
			// Check that the mount is properly formated.
			if numPostFields < 3 {
				return "", fmt.Errorf("Error found less than 3 fields post '-' in %q", text)
			}

			return fields[4], nil
		}
	}
	if err := s.Err(); err != nil {
		return "", err
	}

	return "", NewNotFoundError("Intel RDT")
}

func main(){
	fmt.Println("Initializing goresctrl")
	CheckResctrlSupport();
}
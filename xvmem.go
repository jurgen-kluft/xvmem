package main

import (
	"github.com/jurgen-kluft/xcode"
	"github.com/jurgen-kluft/xvmem/package"
)

func main() {
	xcode.Init()
	xcode.Generate(xvmem.GetPackage())
}

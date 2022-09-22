package asyncprofiler

/*
#cgo CXXFLAGS: -fno-omit-frame-pointer -g -O2 -std=c++11
#cgo darwin CXXFLAGS: -D_XOPEN_SOURCE

extern void async_cgo_context(void *);
extern void async_cgo_traceback(void *);
extern void async_cgo_traceback_internal_set_enabled(int);
*/
import "C"
import "unsafe"

var (
	CgoContext   = unsafe.Pointer(C.async_cgo_context)
	CgoTraceback = unsafe.Pointer(C.async_cgo_traceback)
)

func SetEnabled(status bool) {
	var enabled C.int
	if status {
		enabled = 1
	}
	C.async_cgo_traceback_internal_set_enabled(enabled)
}

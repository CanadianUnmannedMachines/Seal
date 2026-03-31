use std::ffi::{CStr, CString};
use std::os::raw::c_char;

use deno_core::{v8, JsRuntime, RuntimeOptions};

/// Opaque handle passed across the JNI boundary.
pub struct Runtime {
    js: JsRuntime,
}

/// Create a new Deno JS runtime.
///
/// Returns a raw pointer that must eventually be passed to [`deno_runtime_free`].
/// Returns null if initialisation fails (V8 platform init panics are fatal and
/// will abort the process; this function only returns null on logical failures).
#[no_mangle]
pub extern "C" fn deno_runtime_new() -> *mut Runtime {
    let js = JsRuntime::new(RuntimeOptions::default());
    Box::into_raw(Box::new(Runtime { js }))
}

/// Evaluate `source` (null-terminated UTF-8 JS) in the runtime and return the
/// string representation of the result.
///
/// # Safety
/// * `rt_ptr` must be a valid non-null pointer returned by [`deno_runtime_new`].
/// * `source` must be a valid null-terminated UTF-8 string.
/// * The caller must free the returned string with [`deno_string_free`].
#[no_mangle]
pub unsafe extern "C" fn deno_runtime_eval(
    rt_ptr: *mut Runtime,
    source: *const c_char,
) -> *mut c_char {
    let rt = &mut *rt_ptr;

    let source_str = match CStr::from_ptr(source).to_str() {
        Ok(s) => s.to_owned(),
        Err(_) => {
            return error_cstring("source is not valid UTF-8");
        }
    };

    let output = eval_script(&mut rt.js, &source_str);
    CString::new(output)
        .unwrap_or_else(|_| CString::new("Error: CString conversion failed").unwrap())
        .into_raw()
}

/// Destroy a runtime created by [`deno_runtime_new`].
///
/// # Safety
/// `rt_ptr` must be a valid non-null pointer returned by [`deno_runtime_new`] and
/// must not be used after this call.
#[no_mangle]
pub unsafe extern "C" fn deno_runtime_free(rt_ptr: *mut Runtime) {
    if !rt_ptr.is_null() {
        drop(Box::from_raw(rt_ptr));
    }
}

/// Free a string returned by [`deno_runtime_eval`].
///
/// # Safety
/// `s` must be a pointer returned by [`deno_runtime_eval`] and must not be used
/// after this call.
#[no_mangle]
pub unsafe extern "C" fn deno_string_free(s: *mut c_char) {
    if !s.is_null() {
        drop(CString::from_raw(s));
    }
}

// ---------------------------------------------------------------------------
// Internals
// ---------------------------------------------------------------------------

fn eval_script(rt: &mut JsRuntime, source: &str) -> String {
    let result = rt.execute_script("<seal>", source.to_owned());
    match result {
        Err(e) => format!("Error: {e}"),
        Ok(global_val) => {
            // Obtain an owned context handle before taking the mutable isolate
            // reference (sequential borrows — both are fine).
            let context = rt.main_context();
            // `v8_isolate()` returns &mut OwnedIsolate; deref through DerefMut
            // to satisfy HandleScope::new's &mut Isolate parameter.
            let isolate = &mut *rt.v8_isolate();
            let scope = &mut v8::HandleScope::new(isolate);
            let ctx_local = v8::Local::new(scope, context);
            let scope = &mut v8::ContextScope::new(scope, ctx_local);
            let local = v8::Local::new(scope, &global_val);
            match local.to_string(scope) {
                Some(s) => s.to_rust_string_lossy(scope),
                None => String::from("undefined"),
            }
        }
    }
}

fn error_cstring(msg: &str) -> *mut c_char {
    CString::new(format!("Error: {msg}"))
        .unwrap_or_else(|_| CString::new("Error").unwrap())
        .into_raw()
}

import LocalAuthentication
import Foundation

let ctx = LAContext()
var error: NSError?

guard ctx.canEvaluatePolicy(.deviceOwnerAuthentication, error: &error) else {
    fputs("Authentication not available: \(error?.localizedDescription ?? "unknown")\n", stderr)
    exit(2)
}

let sem = DispatchSemaphore(value: 0)
ctx.evaluatePolicy(
    .deviceOwnerAuthentication,
    localizedReason: "envchain: authorize secret access"
) { success, authError in
    if success {
        exit(0)
    } else {
        fputs("Authentication denied: \(authError?.localizedDescription ?? "unknown")\n", stderr)
        exit(1)
    }
}
sem.wait()

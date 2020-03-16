pub enum SlapdError {
    // This occurs when a string contains an inner null byte
    // that cstring can't handle.
    CStringInvalidError,
    FernetInvalidKey,
}

use axum::{
    http::StatusCode,
    response::{IntoResponse, Response},
};
use std::error::Error;
use std::fmt;

#[derive(Debug)]
pub enum AppError {
    VideoNotFound(String),
    InvalidSegmentName,
    InvalidStreamType(String),
    InvalidCodec(String),
    CommandFail(String),
    Error(String),
    NotImplemented,
}

impl fmt::Display for AppError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            AppError::VideoNotFound(msg) => write!(f, "Video Not Found: {}", msg),
            AppError::InvalidSegmentName => write!(f, "Video segment string is wrong"),
            AppError::InvalidStreamType(msg) => write!(f, "Invalid stream type: {}", msg),
            AppError::InvalidCodec(msg) => write!(f, "Invalid codec: {}", msg),
            AppError::CommandFail(msg) => write!(f, "Command failed: {}", msg),
            AppError::Error(msg) => write!(f, "generic error: {}", msg),
            AppError::NotImplemented => write!(f, "not implemented"),
        }
    }
}

impl Error for AppError {}

impl IntoResponse for AppError {
    fn into_response(self) -> Response {
        let status = match &self {
            AppError::VideoNotFound(_msg) => StatusCode::NOT_FOUND,
            AppError::InvalidSegmentName => StatusCode::INTERNAL_SERVER_ERROR,
            AppError::InvalidStreamType(_msg) => StatusCode::INTERNAL_SERVER_ERROR,
            AppError::InvalidCodec(_msg) => StatusCode::INTERNAL_SERVER_ERROR,
            AppError::CommandFail(_msg) => StatusCode::INTERNAL_SERVER_ERROR,
            AppError::Error(_msg) => StatusCode::INTERNAL_SERVER_ERROR,
            AppError::NotImplemented => StatusCode::INTERNAL_SERVER_ERROR,
        };
        (status, self.to_string()).into_response()
    }
}

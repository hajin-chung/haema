use std::{
    ops::{Deref, DerefMut},
    sync::{Arc, Mutex},
};
use tokio::sync::{OwnedSemaphorePermit, Semaphore};

struct Inner<T> {
    items: Mutex<Vec<T>>,
    semaphore: Arc<Semaphore>,
}

pub struct Pool<T> {
    inner: Arc<Inner<T>>,
}

impl<T> Clone for Pool<T> {
    fn clone(&self) -> Self {
        Self {
            inner: self.inner.clone(),
        }
    }
}

impl<T: Send> Pool<T> {
    pub fn new(create_fn: impl Fn() -> T, size: usize) -> Self {
        let mut items_vec = Vec::with_capacity(size);

        for _ in 0..size {
            items_vec.push(create_fn());
        }

        Pool {
            inner: Arc::new(Inner {
                items: Mutex::new(items_vec),
                semaphore: Arc::new(Semaphore::new(size)),
            }),
        }
    }

    pub async fn get(&self) -> PoolGuard<T> {
        let permit = self
            .inner
            .semaphore
            .clone()
            .acquire_owned()
            .await
            .expect("Semaphore closed");

        let item = self
            .inner
            .items
            .lock()
            .unwrap()
            .pop()
            .expect("Pool is empty despite holding a permit");

        PoolGuard {
            item: Some(item),
            pool: self.clone(),
            _permit: permit,
        }
    }

    fn release(&self, item: T) {
        self.inner.items.lock().unwrap().push(item);
    }
}

pub struct PoolGuard<T: Send> {
    item: Option<T>,
    pool: Pool<T>,
    _permit: OwnedSemaphorePermit,
}

impl<T: Send> Drop for PoolGuard<T> {
    fn drop(&mut self) {
        if let Some(item) = self.item.take() {
            self.pool.release(item);
        }
    }
}

// Deref and DerefMut implementations are the same
impl<T: Send> Deref for PoolGuard<T> {
    type Target = T;
    fn deref(&self) -> &Self::Target {
        self.item.as_ref().unwrap()
    }
}

impl<T: Send> DerefMut for PoolGuard<T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        self.item.as_mut().unwrap()
    }
}


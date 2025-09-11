use std::{collections::HashMap, sync::Arc};
use tokio::sync::RwLock;

pub struct Cache<T> {
    map: RwLock<HashMap<String, Arc<T>>>,
}

impl<T> Cache<T> {
    pub async fn get(&self, key: &String) -> Option<Arc<T>> {
        let map = self.map.read().await;
        map.get(key).cloned()
    }

    pub async fn put(&self, key: String, val: Arc<T>) {
        let mut map = self.map.write().await;
        map.insert(key, val);
    }

    pub fn new() -> Self {
        Self {
            map: RwLock::new(HashMap::new()),
        }
    }
}

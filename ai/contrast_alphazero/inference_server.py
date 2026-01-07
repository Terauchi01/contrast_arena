import threading
import time
import queue
from concurrent.futures import Future
import torch


class InferenceServer:
    """Simple batching inference server.

    Usage:
      server = InferenceServer(network, device, max_batch=32, timeout_ms=10)
      server.start()
      fut = server.submit(tensor)  # tensor: torch.Tensor (B=1,...)
      move_logits, tile_logits, value = fut.result()
      server.stop()
    """

    def __init__(self, network, device, max_batch=32, timeout_ms=10):
        self.network = network
        self.device = device
        self.max_batch = int(max_batch)
        self.timeout_ms = int(timeout_ms)
        self._q = queue.Queue()
        self._thread = None
        self._stop = threading.Event()

    def start(self):
        if self._thread is not None:
            return
        # Ensure network is on target device and in eval mode
        self.network.to(self.device)
        self.network.eval()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self):
        if self._thread is None:
            return
        self._stop.set()
        # put a noop to wake thread
        self._q.put(None)
        self._thread.join()
        self._thread = None

    def submit(self, tensor: torch.Tensor) -> Future:
        fut = Future()
        # Expect tensor shape (1, C, H, W) or similar
        self._q.put((tensor, fut))
        return fut

    def _run(self):
        import torch

        while not self._stop.is_set():
            items = []
            first = None
            try:
                first = self._q.get(timeout=self.timeout_ms / 1000.0)
            except queue.Empty:
                continue
            if first is None:
                continue
            items.append(first)

            # drain up to max_batch without blocking much
            while len(items) < self.max_batch:
                try:
                    item = self._q.get_nowait()
                except queue.Empty:
                    break
                if item is None:
                    break
                items.append(item)

            tensors = [t for (t, f) in items]
            futures = [f for (t, f) in items]

            # build batch on device
            try:
                batch = torch.cat([t.to(self.device) for t in tensors], dim=0)
                with torch.no_grad():
                    m_logits, t_logits, values = self.network(batch)

                # move to CPU and split
                m_cpu = m_logits.detach().cpu()
                t_cpu = t_logits.detach().cpu()
                v_cpu = values.detach().cpu()

                for i, fut in enumerate(futures):
                    try:
                        fut.set_result((m_cpu[i:i+1].clone(), t_cpu[i:i+1].clone(), v_cpu[i:i+1].clone()))
                    except Exception as e:
                        fut.set_exception(e)
            except Exception as e:
                # propagate exception to all futures
                for fut in futures:
                    if not fut.done():
                        fut.set_exception(e)
                # small sleep to avoid tight error loop
                time.sleep(0.01)

# type: ignore
"""Swapping implementations for tests with strict override semantics.

A major payoff of routing construction through a container: tests can replace
real infrastructure (databases, HTTP clients, clocks) with fakes by changing
a *registration* instead of monkey-patching modules.

pygim's override rules are strict in both directions, which catches wiring
mistakes early:

- registering a key twice without ``override=True`` raises, and
- ``override=True`` on a key that does NOT exist also raises
  (there must be something to override -- typos fail loudly).

Overriding a singleton also drops its cached instance, so the replacement
takes effect immediately.

This example demonstrates:
- Production wiring in one place, test wiring as a targeted override
- Both directions of the strict override contract
- Singleton cache invalidation on override
"""

from pygim.ioc import Container


# ----------------------------------------------------------------------------
# 1. Application code depends on contracts only
# ----------------------------------------------------------------------------
class EmailGateway:
    """Contract: deliver a message, return a receipt string."""

    def send(self, message):
        raise NotImplementedError


class SmtpGateway(EmailGateway):
    """The 'real' implementation (imagine sockets and retries here)."""

    def send(self, message):
        return f"smtp:{message}"


class SignupFlow:
    def __init__(self, gateway: EmailGateway):
        self.gateway = gateway

    def signup(self, user):
        return self.gateway.send(f"welcome {user}")


def wire_production(container):
    """The single place that knows the production recipe."""
    container.register(EmailGateway, SmtpGateway, lifecycle="singleton")
    container.register(SignupFlow, SignupFlow, autowire=True)


# ----------------------------------------------------------------------------
# 2. Production behaves as wired
# ----------------------------------------------------------------------------
container = Container()
wire_production(container)

flow = container.resolve(SignupFlow)
assert flow.signup("alice") == "smtp:welcome alice"

# The singleton gateway is cached now -- exactly what the override must evict.
assert container.resolve(EmailGateway) is container.resolve(EmailGateway)


# ----------------------------------------------------------------------------
# 3. A test swaps in a fake -- one registration, no monkey-patching
# ----------------------------------------------------------------------------
class FakeGateway(EmailGateway):
    def __init__(self):
        self.sent = []

    def send(self, message):
        self.sent.append(message)
        return f"fake:{message}"


container.register(EmailGateway, FakeGateway,
                   lifecycle="singleton", override=True)
#                                         ▲
#                                         └─ override: required when replacing
#                                            an existing registration

# The stale SmtpGateway singleton was evicted along with the registration, so
# the very next resolve sees the fake -- and the fake records the traffic.
test_flow = container.resolve(SignupFlow)
assert test_flow.signup("bob") == "fake:welcome bob"
assert container.resolve(EmailGateway).sent == ["welcome bob"]

# ----------------------------------------------------------------------------
# 4. The override contract is strict in BOTH directions
# ----------------------------------------------------------------------------
# (a) Re-registering without override=True is always an error...
try:
    container.register(EmailGateway, SmtpGateway)
except RuntimeError as error:
    assert "override" in str(error)
else:
    raise AssertionError("Expected duplicate registration to raise")


# (b) ...and override=True demands an existing registration. A typo in the
# key fails immediately instead of silently creating a parallel service.
class PushGateway(EmailGateway):
    pass


try:
    container.register(PushGateway, PushGateway, override=True)
except RuntimeError as error:
    assert "existing" in str(error)
else:
    raise AssertionError("Expected override of a missing key to raise")

print("IoC override example OK:", container.resolve(EmailGateway).sent)

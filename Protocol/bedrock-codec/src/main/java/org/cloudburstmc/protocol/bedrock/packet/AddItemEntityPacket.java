package org.cloudburstmc.protocol.bedrock.packet;

import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.ToString;
import org.cloudburstmc.math.vector.Vector3f;
import org.cloudburstmc.protocol.bedrock.data.entity.EntityDataMap;
import org.cloudburstmc.protocol.bedrock.data.inventory.ItemData;
import org.cloudburstmc.protocol.common.PacketSignal;

@Data
@EqualsAndHashCode(doNotUseGetters = true)
@ToString(doNotUseGetters = true)
public class AddItemEntityPacket implements BedrockPacket {
    public EntityDataMap metadata = new EntityDataMap();
    public long uniqueEntityId;
    public long runtimeEntityId;
    public ItemData itemInHand;
    public Vector3f position;
    public Vector3f motion;
    public boolean fromFishing;

    @Override
    public final PacketSignal handle(BedrockPacketHandler handler) {
        return handler.handle(this);
    }

    public BedrockPacketType getPacketType() {
        return BedrockPacketType.ADD_ITEM_ENTITY;
    }

    @Override
    public AddItemEntityPacket clone() {
        try {
            return (AddItemEntityPacket) super.clone();
        } catch (CloneNotSupportedException e) {
            throw new AssertionError(e);
        }
    }
}

